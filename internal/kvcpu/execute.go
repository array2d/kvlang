package kvcpu

import (
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
//     ↓ HandleCall 内查 FuncIdx；未找到 → SetError + Notify SysVMErr
func (c *cpu) Execute(pc string) error {
	ctx := context.Background()
	vtid := keytree.VtidFromPC(pc)
	if vtid == "" {
		return fmt.Errorf("Execute: invalid pc %q", pc)
	}

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
			msg := fmt.Sprintf("stack overflow: depth=%d pc=%s", depth, pc)
			vthread.SetError(ctx, c.kv, vtid, pc, msg)
			logx.Debug("[%s] %s", vtid, msg)
			return fmt.Errorf("%s", msg)
		}

		inst, err := op.Decode(ctx, c.kv, pc)
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

		// ── 4. 用户定义函数（default → rewrite as call）─────────────────
		//    不含 dot、不在任何静态集合 → 必然是用户 func
		//    HandleCall 负责 FuncIdx 查找；未找到 → SetError + Notify SysVMErr
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
		newPC, _ := c.kv.Get(keytree.VThreadPC(vtid))
		if newPC == "" {
			break
		}
		pc = newPC
	}
	return nil
}

// RunWorker 单个 worker 的主循环。
func (c *cpu) RunWorker(id int) {
	ctx := context.Background()
	logx.Debug("worker-%d started vmID=%s", id, c.vmID)
	for {
		pc := c.pick(ctx)
		if pc == "" {
			c.wait(ctx)
			continue
		}
		logx.Debug("worker-%d picked pc=%s", id, pc)
		c.Execute(pc)
	}
}

// stackDepth 计算当前调用深度：以 pc 中出现的 [i,j] 段数衡量。
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
