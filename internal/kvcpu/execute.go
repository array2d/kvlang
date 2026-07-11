// Package kvcpu 提供 KV CPU 执行引擎。
package kvcpu

import (
	"context"
	"fmt"

	"kvlang/internal/kvspace"
	"kvlang/internal/logx"
	"kvlang/internal/op"
	"kvlang/internal/op/builtin"
	"kvlang/internal/op/dispatch"
	"kvlang/internal/vthread"
	"kvlang/internal/vtype"

	// 触发 str、tensor vtype 的 init() 注册
	_ "kvlang/internal/vtype"
)

// RunWorker 单个 worker 的主循环。
func RunWorker(ctx context.Context, kv kvspace.KVSpace, id int) {
	logx.Debug("worker-%d started", id)
	for {
		select {
		case <-ctx.Done():
			logx.Debug("worker-%d stopped", id)
			return
		default:
		}
		vtid := Pick(ctx, kv)
		if vtid == "" {
			Wait(ctx, kv)
			continue
		}
		logx.Debug("worker-%d picked vthread %s", id, vtid)
		Execute(ctx, kv, vtid)
	}
}

// Execute 执行一个 vthread 直到完成或出错。
//
// Dispatch 优先级（全静态，无 KV 分类查询）：
//  1. IsControlOp   — call/return/if/br/goto 等控制流关键字
//  2. IsNativeOp    — +/-/*/print/sqrt/str.set 等标量内建算子
//  3. vtype.Lookup  — tensor.*、str.* 等命名空间算子（前缀匹配）
//  4. default       — 用户定义函数（无 dot、无关键字 → 必为 func）
func Execute(ctx context.Context, kv kvspace.KVSpace, vtid string) {
	for {
		s := vthread.Get(ctx, kv, vtid)
		if s.Status == "done" || s.Status == "error" {
			return
		}
		pc := s.PC
		inst, err := op.Decode(ctx, kv, vtid, pc)
		if err != nil {
			logx.Debug("[%s] decode error at %s: %v", vtid, pc, err)
			vthread.SetError(ctx, kv, vtid, pc, fmt.Sprintf("decode: %v", err))
			return
		}
		if inst.Opcode == "" {
			logx.Debug("[%s] done at %s", vtid, pc)
			vthread.Set(ctx, kv, vtid, pc, "done")
			return
		}
		logx.Debug("[%s] PC=%s OP=%s READS=%v WRITES=%v", vtid, pc, inst.Opcode, inst.Reads, inst.Writes)

		var execErr error
		switch {

		// ── 1. 控制流关键字（静态集合，零 KV 查询）──────────────────────
		case op.IsControlOp(inst.Opcode):
			execErr = handleControl(ctx, kv, vtid, pc, inst)

		// ── 2. 标量内建算子（静态 map，零 KV 查询）───────────────────────
		//    覆盖：+ - * / print sqrt int float bool str.set 等
		case builtin.IsNativeOp(inst.Opcode):
			execErr = builtin.Native(ctx, kv, vtid, pc, inst)

		// ── 3. VType 命名空间算子（前缀匹配，零 KV 查询）─────────────────
		//    tensor.*  →  tensorVType → heap-plat 或 op-plat
		//    str.*     →  strVType    → builtin（未注册于 nativeOps 的新 str op）
		case vtype.Lookup(inst.Opcode) != nil:
			execErr = vtype.Lookup(inst.Opcode).Exec(ctx, kv, vtid, pc, inst)

		// ── 4. 用户定义函数（default，无任何 KV 分类查询）───────────────
		//    不含 dot、不在任何静态集合 → 必然是用户 func
		//    HandleCall 负责 FuncIdx 查找；不存在则返回清晰错误
		default:
			logx.Debug("[%s] user func: %s", vtid, inst.Opcode)
			inst.Reads = append([]string{inst.Opcode}, inst.Reads...)
			inst.Opcode = op.OpCall
			execErr = handleControl(ctx, kv, vtid, pc, inst)
		}

		if execErr != nil {
			logx.Debug("[%s] error: %v", vtid, execErr)
			return
		}
	}
}

var _ = dispatch.Lifecycle // 确保 dispatch 包链接
