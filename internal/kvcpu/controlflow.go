package kvcpu

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/layoutcode"
	"kvlang/internal/logx"
	"kvlang/internal/op"
	"kvlang/internal/op/builtin"
	"kvlang/internal/vthread"
)

// handleControl 分发控制流原语（call / return / br / goto）。
// pc 为绝对路径，inst 已解码。
// if / for / while 等高级语法由编译器降级为 br，不在此处理。
func handleControl(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	switch inst.Opcode {
	case op.OpCall:
		substackPC := layoutcode.HandleCall(ctx, kv, pc, inst, false)
		if substackPC == pc {
			return fmt.Errorf("call %s failed", inst.Reads[0])
		}
		vthread.Set(ctx, kv, vtid, substackPC, "running")
		logx.Debug("[%s] CALL → %s", vtid, substackPC)
		return nil

	case op.OpReturn:
		parentPC, retVal := layoutcode.HandleReturn(ctx, kv, pc, inst)
		logx.Debug("[%s] RETURN parentPC=%q retVal=%q", vtid, parentPC, retVal)
		if parentPC == "" {
			// 顶层 return → vthread 完成，retVal 成为 .status 终态通知值
			vthread.SetDone(ctx, kv, vtid, retVal)
			return nil
		}
		vthread.Set(ctx, kv, vtid, parentPC, "running")
		return nil

	case op.OpBr:
		return brToCall(ctx, kv, vtid, pc, inst)

	default:
		return fmt.Errorf("unknown control op: %s", inst.Opcode)
	}
}

// brToCall 处理 br(cond, trueLabel, falseLabel)：
// 根据条件选择分支，rewrite 为 call 进入目标块。
func brToCall(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	if len(inst.Reads) < 3 {
		return fmt.Errorf("br requires 3 args: cond trueLabel falseLabel")
	}
	framePath := keytree.FrameRoot(pc)
	condVal := builtin.ResolveReadValue(kv, framePath, inst.Reads[0])
	isTrue := condVal != "" && condVal != "0" && condVal != "false"
	label := inst.Reads[2]
	if isTrue {
		label = inst.Reads[1]
	}
	qualified := resolveLabel(kv, framePath, label)
	callInst := &op.Instruction{Opcode: op.OpCall, Reads: []string{qualified}}
	substackPC := layoutcode.HandleCall(ctx, kv, pc, callInst, false)
	if substackPC == pc {
		return fmt.Errorf("br call %s failed", qualified)
	}
	vthread.Set(ctx, kv, vtid, substackPC, "running")
	return nil
}

// resolveLabel 在函数上下文中将 label 解析为完整函数路径。
// 若 label 已包含 / 则直接使用；否则从帧的 .func 字段取当前函数名拼接。
func resolveLabel(kv kvspace.KVSpace, framePath, label string) string {
	if strings.Contains(label, "/") {
		return label
	}
	// 当前函数名由 HandleCall 存入 framePath/.func
	if funcName, err := kv.Get(framePath + "/.func"); err == nil && funcName != "" {
		qualified := funcName + "/" + label
		if pkg, err := kv.Get(keytree.FuncIdx(qualified)); err == nil && pkg != "" {
			return qualified
		}
	}
	return label
}
