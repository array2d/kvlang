package kvcpu

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/op"
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

// funcRoot 提取当前 PC 的函数根路径。
// [0,0]/entry/[0,0] → [0,0]; [0,0] → [0,0]
func funcRoot(pc string) string {
	if idx := strings.LastIndex(pc, "/"); idx >= 0 {
		// 检查最后一段是否是 block label（非 [n,m] 格式）
		last := pc[idx+1:]
		if len(last) > 0 && last[0] != '[' {
			return pc[:idx] // 去掉 /label
		}
	}
	return pc
}

// blockPC 构造跳转目标: funcRoot + label + "/[0,0]"
func blockPC(pc, label string) string {
	return funcRoot(pc) + "/" + label + "/[0,0]"
}

// Br 处理条件分支: br(cond, true_label, false_label).
func Br(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	if len(inst.Reads) < 3 { return fmt.Errorf("br requires 3 args") }
	condVal := readCond(kv, vtid, inst.Reads[0])
	isTrue := condVal != "" && condVal != "0" && condVal != "false"
	target := blockPC(pc, inst.Reads[2])
	if isTrue { target = blockPC(pc, inst.Reads[1]) }
	return jumpOrNext(ctx, kv, vtid, pc, target)
}

// Goto 处理无条件跳转: goto(label).
func Goto(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	if len(inst.Reads) < 1 { return fmt.Errorf("goto requires label") }
	return jumpOrNext(ctx, kv, vtid, pc, blockPC(pc, inst.Reads[0]))
}

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
