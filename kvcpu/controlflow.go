package kvcpu

import (
	"context"
	"fmt"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/layout"
	"kvlang/logx"
	"kvlang/rwir"
	"kvlang/rwir/builtin"
	"kvlang/vthread"
)

// handleControl 分发控制流原语（call / return / br / goto）。
func handleControl(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *rwir.Rwir) error {
	switch inst.Opcode {
	case rwir.OpCall:
		substackPC := layout.HandleCall(ctx, kv, pc, inst)
		if substackPC == "" {
			return fmt.Errorf("call %s failed", inst.Reads[0].Name)
		}
		vthread.Set(ctx, kv, vtid, substackPC, "running")
		logx.Debug("[%s] CALL → %s", vtid, substackPC)
		return nil

	case rwir.OpReturn:
		parentPC, retVal := layout.HandleReturn(ctx, kv, pc, inst)
		logx.Debug("[%s] RETURN parentPC=%q retVal=%q", vtid, parentPC, retVal)
		if parentPC == "" {
			vthread.SetDone(ctx, kv, vtid, retVal)
			return nil
		}
		vthread.Set(ctx, kv, vtid, parentPC, "running")
		return nil

	case rwir.OpGoto:
		return gotoBlock(ctx, kv, vtid, pc, inst)

	case rwir.OpBr:
		return brBlock(ctx, kv, vtid, pc, inst)

	default:
		return fmt.Errorf("unknown control op: %s", inst.Opcode)
	}
}

// gotoBlock 处理 goto(scope)：复用或创建 scope 帧，PC 跳转。
func gotoBlock(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *rwir.Rwir) error {
	if len(inst.Reads) == 0 {
		return fmt.Errorf("goto requires scope")
	}
	newPC := layout.HandleScope(ctx, kv, pc, inst.Reads[0].Name)
	if newPC == "" {
		msg := fmt.Sprintf("RuntimeError: goto %s failed", inst.Reads[0].Name); vthread.SetError(ctx, kv, vtid, pc, msg); return fmt.Errorf("%s", inst.Reads[0].Name)
	}
	vthread.Set(ctx, kv, vtid, newPC, "running")
	logx.Debug("[%s] GOTO → %s", vtid, newPC)
	return nil
}

// brBlock 处理 br(cond, trueScope, falseScope)：条件分支，复用或创建 scope 帧。
func brBlock(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *rwir.Rwir) error {
	if len(inst.Reads) < 3 {
		return fmt.Errorf("br requires 3 args: cond trueLabel falseLabel")
	}
	condVal := builtin.ResolveReadValue(kv, keytree.FrameRoot(pc), inst.Reads[0].Name)
	if condVal.IsNone() {
		msg := "TypeError: None in branch condition"
		vthread.SetError(ctx, kv, vtid, pc, msg)
		return fmt.Errorf("%s", msg)
	}
	label := inst.Reads[2].Name
	if builtin.AsBool(condVal) {
		label = inst.Reads[1].Name
	}
	newPC := layout.HandleScope(ctx, kv, pc, label)
	if newPC == "" {
		msg := fmt.Sprintf("RuntimeError: br %s failed", label); vthread.SetError(ctx, kv, vtid, pc, msg); return fmt.Errorf("%s", label)
	}
	vthread.Set(ctx, kv, vtid, newPC, "running")
	return nil
}
