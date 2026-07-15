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
		if substackPC == "" {
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

	case op.OpGoto:
		return gotoBlock(ctx, kv, vtid, pc, inst)

	case op.OpBr:
		return brToCall(ctx, kv, vtid, pc, inst)

	default:
		return fmt.Errorf("unknown control op: %s", inst.Opcode)
	}
}

// gotoBlock 处理 goto(label)：TCO 跳转至同函数内的目标块。
// 复用当前帧（不创建子帧），重新链接 _fn 到目标块代码区。
func gotoBlock(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	if len(inst.Reads) == 0 {
		return fmt.Errorf("goto requires label")
	}
	framePath := keytree.FrameRoot(pc)
	label := resolveLabel(kv, framePath, inst.Reads[0])
	callInst := &op.Instruction{Opcode: op.OpCall, Reads: []string{label}}
	substackPC := layoutcode.HandleCall(ctx, kv, pc, callInst, true)
	if substackPC == "" {
		return fmt.Errorf("goto %s failed", label)
	}
	vthread.Set(ctx, kv, vtid, substackPC, "running")
	logx.Debug("[%s] GOTO → %s", vtid, substackPC)
	return nil
}

// brToCall 处理 br(cond, trueLabel, falseLabel)：
// 根据条件选择分支，TCO 跳转至目标块。
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
	substackPC := layoutcode.HandleCall(ctx, kv, pc, callInst, true)
	if substackPC == "" {
		return fmt.Errorf("br call %s failed", qualified)
	}
	vthread.Set(ctx, kv, vtid, substackPC, "running")
	return nil
}

// resolveLabel 在函数上下文中将 label 解析为完整函数路径。
//
// 优先级：
//  1. label 已含 "/" → 直接返回（编译器生成的完全限定标签）
//  2. /.rootfunc + "/" + label → 根函数级别（用户书写的裸标签，TCO 不影响）
//  3. /.func + "/" + label → 当前块级别（保留兼容性）
func resolveLabel(kv kvspace.KVSpace, framePath, label string) string {
	if strings.Contains(label, "/") {
		return label
	}
	// 优先用根函数名（TCO 不改变 .rootfunc）
	if rootFunc, err := kv.Get(framePath + "/.rootfunc"); err == nil && rootFunc != "" {
		qualified := rootFunc + "/" + label
		if pkg, err := kv.Get(keytree.FuncIdx(qualified)); err == nil && pkg != "" {
			return qualified
		}
	}
	// 兼容旧路径：用当前块函数名
	if funcName, err := kv.Get(framePath + "/.func"); err == nil && funcName != "" {
		qualified := funcName + "/" + label
		if pkg, err := kv.Get(keytree.FuncIdx(qualified)); err == nil && pkg != "" {
			return qualified
		}
	}
	return label
}
