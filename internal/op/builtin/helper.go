package builtin

import (
	"fmt"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

func requireBinary(inputs []nativeValue) error {
	if len(inputs) != 2 { return fmt.Errorf("binary op requires 2 inputs, got %d", len(inputs)) }
	return nil
}
func requireUnary(inputs []nativeValue) error {
	if len(inputs) != 1 { return fmt.Errorf("unary op requires 1 input, got %d", len(inputs)) }
	return nil
}

// readInputs 从 KV 读取 inst.Reads 对应的值。
// 支持：./x（显式相对槽）、/abs（绝对路径）、3（数字字面量）、x（裸 ident，查本帧槽）。
func readInputs(f *op.Frame) []nativeValue {
	framePath := keytree.FrameRoot(f.PC)
	inputs := make([]nativeValue, 0, len(f.Inst.Reads))
	for _, r := range f.Inst.Reads {
		raw := resolveReadValue(f.KV, framePath, r)
		inputs = append(inputs, parseNativeValue(raw))
	}
	return inputs
}

// writeResult 写回计算结果并推进 PC。
func writeResult(f *op.Frame, result nativeValue) error {
	if len(f.Inst.Writes) > 0 {
		outKey := resolveWriteKey(keytree.FrameRoot(f.PC), f.Inst.Writes[0])
		if err := f.KV.Set(outKey, result.String()); err != nil { return err }
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

// nextPC 推进 PC，不写结果。
func nextPC(f *op.Frame) {
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
}

// ExecuteCopy 执行路径/变量复制：读取 inst.Opcode 引用的值，写入所有 writes 槽。
// 用于处理 ./A -> ./gcd 形式的赋值指令（opcode = "./A"，无其他参数）。
func ExecuteCopy(kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	framePath := keytree.FrameRoot(pc)
	val := resolveReadValue(kv, framePath, inst.Opcode)
	for _, w := range inst.Writes {
		key := resolveWriteKey(framePath, w)
		if err := kv.Set(key, val); err != nil {
			return err
		}
	}
	vthread.Set(bg, kv, vtid, op.NextPC(pc), "running")
	return nil
}
