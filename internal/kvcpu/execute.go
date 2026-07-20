package kvcpu

import (
	"strings"
	"context"
	"fmt"

	"kvlang/internal/keytree"
	"kvlang/internal/logx"
	"kvlang/internal/op"
	"kvlang/internal/op/builtin"
	"kvlang/internal/vthread"
	"kvlang/internal/vtype"

	// 触发 str、tensor vtype 的 init() 注册
	_ "kvlang/internal/vtype"
)

// MaxStackDepth 允许的最大调用栈深度（P6）。
// 超过此深度触发 stack overflow error。
const MaxStackDepth = 256

// Execute 从绝对 PC 开始执行 vthread，直到完成、出错或 ctx 取消。
//
// Dispatch 优先级（全静态，无 KV 分类查询）：
//  1. IsControlOp   — call/return/br/goto 控制流原语
//  2. IsNativeOp    — +/-/*/print/sqrt 等标量内建算子
//  3. vtype.Lookup  — tensor.*、str.* 等命名空间算子
//  4. default       — 用户定义函数（rewrite as call）
//     ↓ HandleCall 内查 FuncIdx；未找到 → SetError
//
// 调试支持（内置，无需特殊启动）：
// agent 在任意时刻通过 kvspace 写入 /vthread/<vtid>/.debugger 即可激活调试模式。
// CPU 在函数入口处检查该标志；激活后每条指令均暂停，等待 .debug.resume 命令。
func (c *cpu) Execute(pc string) error {
	ctx := context.Background()
	vtid := keytree.VtidFromPC(pc)
	if vtid == "" {
		return fmt.Errorf("Execute: invalid pc %q", pc)
	}

	// stepping 是本次 vthread 执行的局部状态：
	// true  = 单步模式（每条指令执行后暂停）
	// false = 正常模式（仅在函数入口检查 .debugger 标志）
	// Execute 每次调用对应一个 vthread，单 goroutine 执行，无需加锁。
	stepping := false

	for {
		_, status := vthread.Get(ctx, c.kv, vtid)
		// .status 正常运行态：init | running | wait
		// 终态时 .status 已 Del+Notify，Get 返回空串或读取失败 → 退出循环
		switch status {
		case "init", "running", "wait":
			// 继续执行
		default:
			return nil // 已终态（含读取失败）
		}

		// P6：栈深度保护
		depth := stackDepth(pc)
		if depth > MaxStackDepth {
			msg := fmt.Sprintf("RecursionError: stack overflow: depth=%d pc=%s; help: remove infinite recursion or reorganize as a loop", depth, pc)
			vthread.SetError(ctx, c.kv, vtid, pc, msg)
			logx.Debug("[%s] %s", vtid, msg)
			return fmt.Errorf("%s", msg)
		}

		linkBase := keytree.FuncLib(keytree.FrameRoot(pc))
		inst, err := op.Decode(ctx, c.kv, linkBase, pc)
		if err != nil {
			logx.Debug("[%s] decode error at %s: %v", vtid, pc, err)
			vthread.SetError(ctx, c.kv, vtid, pc, fmt.Sprintf("decode: %v", err))
			return err
		}
		if inst.Opcode == "" {
			logx.Debug("[%s] empty opcode at %s → done", vtid, pc)
			vthread.SetDone(ctx, c.kv, vtid, "ok")
			return nil
		}
		logx.Debug("[%s] PC=%s OP=%s READS=%v WRITES=%v", vtid, pc, inst.Opcode, inst.Reads, inst.Writes)

		// ── 内联调试检查 ──────────────────────────────────────────────────
		// 检查点：decode 之后、dispatch 之前。
		// 此时 KV 空间处于稳定状态（上一条指令已完成，当前指令尚未执行），
		// agent 通过 kvspace 读取到的是一致的内存快照。
		//
		// 性能策略：
		//   - 非单步模式：仅在函数入口（isEntryPC）读取一次 .debug（每次函数调用 1 次）
		//   - 单步模式：每条指令读取一次 .debug（已在调试中，overhead 可接受）
		if stepping || isEntryPC(pc) {
			v, _ := c.kv.Get(keytree.VThreadDebugger(vtid))
			switch mode := v.Str(); {
			case mode == "" && stepping:
				// Agent 清除了 .debugger 标志 → 退出单步模式
				stepping = false
				logx.Debug("[%s] debug: stepping deactivated", vtid)
			case mode == "step":
				// .debugger 标志已设置 → 暂停
				if !stepping {
					stepping = true
					logx.Debug("[%s] debug: stepping activated at %s", vtid, pc)
				}
				debugNotifyPause(ctx, c.kv, vtid, pc, inst)
				switch cmd := debugWaitResume(c.kv, vtid); cmd {
				case "abort":
					vthread.SetError(ctx, c.kv, vtid, pc, "RuntimeError: debug: aborted by agent")
					return fmt.Errorf("RuntimeError: debug: aborted by agent")
				case "continue":
					stepping = false
					c.kv.Del(keytree.VThreadDebugger(vtid)) // 清除标志，恢复全速
					logx.Debug("[%s] debug: continue → stepping off", vtid)
				// "step" 或其他 → 保持单步
				}
			}
		}

		// 读参写保护（fix-027）：裸名写槽命中帧 .ro 名单 → 异常终止
		if err := c.checkReadOnlyWrites(ctx, vtid, pc, inst); err != nil {
			return err
		}

		var execErr error
		switch {

		// ── 1. 控制流原语（静态集合，零 KV 查询）──────────────────────────
		case op.IsControlOp(inst.Opcode):
			execErr = handleControl(ctx, c.kv, vtid, pc, inst)

		// ── 2. 标量内建算子（静态 map，零 KV 查询）──────────────────────
		case builtin.IsNativeOp(inst.Opcode):
			execErr = builtin.Native(ctx, c.kv, vtid, pc, inst)

		// ── 3. VType 命名空间算子（前缀匹配，零 KV 查询）────────────────
		case vtype.Lookup(inst.Opcode) != nil:
			execErr = vtype.Lookup(inst.Opcode).Exec(ctx, c.kv, vtid, pc, inst)

		// ── 4. 路径/变量复制（ ./x -> dst 或 /abs -> dst 或 a -> b）──────
		//    当 opcode 为路径或字面量且有写槽时，视为 copy 操作。
		//    裸标识符由 Flat() 归一化为 ./ident，此处通过路径检查统一识别。
		case isCopyOp(inst.Opcode, inst.Writes):
			execErr = builtin.ExecuteCopy(c.kv, vtid, pc, inst)

		// ── 5. 用户定义函数（default → rewrite as call）─────────────────
		//    不含 dot、不在任何静态集合 → 必然是用户 func
		//    HandleCall 负责 FuncIdx 查找；未找到 → SetError
		default:
			logx.Debug("[%s] user func: %s", vtid, inst.Opcode)
			inst.Reads = append([]string{inst.Opcode}, inst.Reads...)
			inst.Opcode = op.OpCall
			execErr = handleControl(ctx, c.kv, vtid, pc, inst)
		}

		if execErr != nil {
			logx.Debug("[%s] execErr: %v", vtid, execErr)
			return execErr
		}

		// 读取指令执行后更新的 PC
		newPCVal, _ := c.kv.Get(keytree.VThreadPC(vtid))
		newPC := newPCVal.Str()
		if newPC == "" {
			break
		}
		pc = newPC
	}
	return nil
}

// isCopyOp reports whether this instruction is a value-copy rather than a call.
//
// Copy 指令由 Flat() 编码为显式操作码 "="：<value-ref> -> slot
// 值引用在读槽（bare ident / literal / /abs），由 ExecuteCopy → resolveReadValue 解析。
func isCopyOp(opcode string, writes []string) bool {
	return opcode == "=" && len(writes) > 0
}

// checkReadOnlyWrites 读参只读公理的运行期防线（fix-027）：
// 带写槽的指令，裸名写槽（无 / . [ 形态）命中当前帧 .ro 名单（Bootstrap/HandleCall 写入）
// → SetError 异常终止。set 的 `-> base` 本体回写（写回原值，fix-013）豁免。
// 编译期 parser 已阻断源码路径，此处兜底 agent 直写 KV 构造的指令。
func (c *cpu) checkReadOnlyWrites(ctx context.Context, vtid, pc string, inst *op.Instruction) error {
	if len(inst.Writes) == 0 {
		return nil
	}
	roVal, _ := c.kv.Get(keytree.FrameRO(keytree.FrameRoot(pc)))
	ro := roVal.Str()
	if ro == "" {
		return nil
	}
	names := strings.Split(ro, ",")
	for i, w := range inst.Writes {
		if strings.ContainsAny(w, "/.[") {
			continue
		}
		if inst.Opcode == "set" && i == 0 && len(inst.Reads) > 0 && w == inst.Reads[0] {
			continue
		}
		for _, n := range names {
			if w == n {
				msg := fmt.Sprintf("ReadOnlyError: read-only param %q cannot be used as write slot; help: declare it as a write param (accumulator) or copy to a local", w)
				vthread.SetError(ctx, c.kv, vtid, pc, msg)
				return fmt.Errorf("%s", msg)
			}
		}
	}
	return nil
}

// stackDepth计算当前调用深度：以 pc 中出现的 [i,j] 段数衡量。
// /vthread/42/[0,0] → depth=1（顶层）
// /vthread/42/[3,0]/[0,0] → depth=2
func stackDepth(pc string) int {
	depth := 0
	for i := 0; i < len(pc); i++ {
		if pc[i] == '[' {
			depth++
		}
	}
	return depth
}
