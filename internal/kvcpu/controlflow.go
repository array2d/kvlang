package kvcpu

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/op"
	"kvlang/internal/layoutcode"
	"kvlang/internal/logx"
	"kvlang/internal/vthread"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

// If 处理 if 控制流指令。
func If(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	if len(inst.Reads) < 1 { return fmt.Errorf("if requires condition") }
	condVal := readCond(kv, vtid, inst.Reads[0])
	isTrue := condVal != "" && condVal != "0" && condVal != "false"
	target := ""
	if isTrue && len(inst.Reads) > 1 { target = inst.Reads[1] }
	if !isTrue && len(inst.Reads) > 2 { target = inst.Reads[2] }
	return jumpOrNext(ctx, kv, vtid, pc, target)
}

// Goto 处理无条件跳转: goto(label).


func readCond(kv kvspace.KVSpace, vtid, key string) string {
	if strings.HasPrefix(key, "./") {
		val, err := kv.Get(keytree.VThreadAt(vtid, key[2:]))
		if err == nil { return val }
	}
	return key
}

func jumpOrNext(ctx context.Context, kv kvspace.KVSpace, vtid, pc, target string) error {
	if target == "" {
		logx.Debug("[%s] branch → next PC", vtid)
		vthread.Set(ctx, kv, vtid, op.NextPC(pc), "running")
		return nil
	}
	logx.Debug("[%s] branch → %s", vtid, target)
	vthread.Set(ctx, kv, vtid, target, "running")
	return nil
}

func handleControl(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	switch inst.Opcode {
	case op.OpCall:
		substackPC := layoutcode.HandleCall(ctx, kv, vtid, pc, inst, false)
		if substackPC == pc {
			return fmt.Errorf("call %s failed", inst.Reads[0])
		}
		vthread.Set(ctx, kv, vtid, substackPC, "running")
		logx.Debug("[%s] CALL → %s", vtid, substackPC)
		return nil
	case op.OpReturn:
		parentPC := layoutcode.HandleReturn(ctx, kv, vtid, pc, inst)
		logx.Debug("[%s] RETURN → %s", vtid, parentPC)
		if parentPC == pc {
			vthread.Set(ctx, kv, vtid, pc, "done")
			return nil
		}
		vthread.Set(ctx, kv, vtid, parentPC, "running")
		return nil
		case op.OpIf:
			return If(ctx, kv, vtid, pc, inst)
		case op.OpBr:
			return brToCall(ctx, kv, vtid, pc, inst)
		default:
			return fmt.Errorf("unknown control op: %s", inst.Opcode)
		}
	}

func brToCall(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	if len(inst.Reads) < 3 { return fmt.Errorf("br requires 3 args") }
	condVal := readCond(kv, vtid, inst.Reads[0])
	isTrue := condVal != "" && condVal != "0" && condVal != "false"
	label := inst.Reads[2]
	if isTrue { label = inst.Reads[1] }
	// label 为简单名 → 尝试查找 parent/label；若不存在则直接用 label
	qualified := resolveLabel(kv, vtid, pc, label)
	callInst := &op.Instruction{Opcode: op.OpCall, Reads: []string{qualified}}
	substackPC := layoutcode.HandleCall(ctx, kv, vtid, pc, callInst, false)
	if substackPC == pc { return fmt.Errorf("br call %s failed", qualified) }
	vthread.Set(ctx, kv, vtid, substackPC, "running")
	return nil
}

// resolveLabel 在函数上下文中解析 label 为完整路径。
func resolveLabel(kv kvspace.KVSpace, vtid, pc, label string) string {
	// 若 label 已含 / → 直接使用
	if strings.Contains(label, "/") { return label }
	// 从 vthread 入口槽取当前函数名 → 拼接 funcName/label
	entryKey := keytree.VThreadSlot(vtid, 0, 0)
	if funcName, err := kv.Get(entryKey); err == nil {
		qualified := funcName + "/" + label
		// 通过反向索引验证块是否存在
		if pkg, err := kv.Get(keytree.FuncIdx(qualified)); err == nil && pkg != "" {
			return qualified
		}
	}
	return label
}
