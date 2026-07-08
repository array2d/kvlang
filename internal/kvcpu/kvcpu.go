// Package kvcpu 提供 VM 核心执行循环与控制流指令。
package kvcpu

import (
	"context"
	"fmt"

	"kvlang/internal/codegen"
	"kvlang/internal/op/dispatch"
	"kvlang/internal/op"
	"kvlang/internal/logx"
	"kvlang/internal/vthread"
	"kvlang/internal/op/builtin"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
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
		case op.IsControlOp(inst.Opcode):
			execErr = handleControl(ctx, kv, vtid, pc, inst)
		case builtin.IsNativeOp(inst.Opcode):
			execErr = builtin.Native(ctx, kv, vtid, pc, inst)
		case op.IsLifecycleOp(inst.Opcode):
			execErr = dispatch.Lifecycle(ctx, kv, vtid, pc, inst)
		case isFunctionCall(ctx, kv, inst.Opcode):
			inst.Reads = append([]string{inst.Opcode}, inst.Reads...)
			inst.Opcode = op.OpCall
			execErr = handleControl(ctx, kv, vtid, pc, inst)
		case op.IsComputeOp(inst.Opcode):
			execErr = dispatch.Compute(ctx, kv, vtid, pc, inst)
		default:
			vthread.Set(ctx, kv, vtid, op.NextPC(pc), "running")
		}
		if execErr != nil {
			logx.Debug("[%s] error: %v", vtid, execErr)
			return
		}
	}
}

func handleControl(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	switch inst.Opcode {
	case op.OpCall:
		substackPC := codegen.HandleCall(ctx, kv, vtid, pc, inst)
		if substackPC == pc {
			// HandleCall already set error state (func not found, parse failure, etc.)
			return fmt.Errorf("call %s failed", inst.Reads[0])
		}
		vthread.Set(ctx, kv, vtid, substackPC, "running")
		logx.Debug("[%s] CALL → %s", vtid, substackPC)
		return nil
	case op.OpReturn:
		parentPC := codegen.HandleReturn(ctx, kv, vtid, pc)
		logx.Debug("[%s] RETURN → %s", vtid, parentPC)
		if parentPC == pc {
			vthread.Set(ctx, kv, vtid, pc, "done")
			return nil
		}
		vthread.Set(ctx, kv, vtid, parentPC, "running")
		return nil
	case op.OpIf:
		return If(ctx, kv, vtid, pc, inst)
	default:
		return fmt.Errorf("unknown control op: %s", inst.Opcode)
	}
}

func isFunctionCall(ctx context.Context, kv kvspace.KVSpace, opcode string) bool {
	if _, err := kv.Get(keytree.SrcFunc(opcode)); err == nil {
		return true
	}
	for _, backend := range []string{"op-metal", "op-cuda", "op-cpu"} {
		if _, err := kv.Get(keytree.OpBackendFunc(backend, opcode)); err == nil {
			return true
		}
	}
	return false
}
