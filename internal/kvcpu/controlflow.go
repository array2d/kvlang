package kvcpu

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/internal/layoutrwir"
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
		substackPC := layoutrwir.HandleCall(ctx, kv, pc, inst, false)
		if substackPC == "" {
			return fmt.Errorf("call %s failed", inst.Reads[0])
		}
		vthread.Set(ctx, kv, vtid, substackPC, "running")
		logx.Debug("[%s] CALL → %s", vtid, substackPC)
		return nil

	case op.OpReturn:
		parentPC, retVal := layoutrwir.HandleReturn(ctx, kv, pc, inst)
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
	substackPC := layoutrwir.HandleCall(ctx, kv, pc, callInst, true)
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
	isTrue := builtin.AsBool(condVal)
	label := inst.Reads[2]
	if isTrue {
		label = inst.Reads[1]
	}
	qualified := resolveLabel(kv, framePath, label)
	callInst := &op.Instruction{Opcode: op.OpCall, Reads: []string{qualified}}
	substackPC := layoutrwir.HandleCall(ctx, kv, pc, callInst, true)
	if substackPC == "" {
		return fmt.Errorf("br call %s failed", qualified)
	}
	vthread.Set(ctx, kv, vtid, substackPC, "running")
	return nil
}

// resolveLabel 在函数上下文中将 label 解析为完整函数路径。
//
// 优先级：
//  1. label 已含 "/" → 直接返回（编译器生成的完全限定标签；lower 保证所有 br/goto 走此路径）
//  2. /.rootfunc + "/" + label → 根函数名（TCO 不更新 .rootfunc，用户裸标签可安全解析）
func resolveLabel(kv kvspace.KVSpace, framePath, label string) string {
	if strings.Contains(label, "/") {
		return label
	}
	if v, err := kv.Get(keytree.RootFunc(framePath)); err == nil {
		if rootFunc := v.Str(); rootFunc != "" {
			qualified := rootFunc + "/" + label
			if pv, err := kv.Get(keytree.LibIdx(qualified)); err == nil && pv.Str() != "" {
				return qualified
			}
		}
	}
	return label
}
