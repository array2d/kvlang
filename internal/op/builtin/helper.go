package builtin

import (
	"fmt"

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
	inputs := make([]nativeValue, 0, len(f.Inst.Reads))
	for _, r := range f.Inst.Reads {
		raw := resolveReadValue(f.KV, f.Vtid, r)
		inputs = append(inputs, parseNativeValue(raw))
	}
	return inputs
}

// writeResult 写回计算结果并推进 PC。
func writeResult(f *op.Frame, result nativeValue) error {
	if len(f.Inst.Writes) > 0 {
		outKey := resolveWriteKey(f.Vtid, f.Inst.Writes[0])
		if err := f.KV.Set(outKey, result.String()); err != nil { return err }
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

// nextPC 推进 PC，不写结果。
func nextPC(f *op.Frame) {
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
}
