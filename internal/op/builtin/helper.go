package builtin

import (
	"fmt"

	"kvlang/internal/keytree"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

// readInputs 从 KV 读取 inst.Reads 对应的值。
func readInputs(f *op.Frame) []nativeValue {
	inputs := make([]nativeValue, 0, len(f.Inst.Reads))
	for _, r := range f.Inst.Reads {
		var raw string
		if isRelative(r) {
			key := keytree.VThreadAt(f.Vtid, r[2:])
			val, err := f.KV.Get(key)
			if err != nil {
				vthread.SetError(bg, f.KV, f.Vtid, f.PC, fmt.Sprintf("read %s: %v", key, err))
				return nil
			}
			raw = val
		} else {
			raw = r
		}
		inputs = append(inputs, parseNativeValue(raw))
	}
	return inputs
}

// writeResult 写回计算结果并推进 PC。
func writeResult(f *op.Frame, result nativeValue) error {
	if len(f.Inst.Writes) > 0 {
		outKey := resolveWriteKey(f.Vtid, f.Inst.Writes[0])
		if err := f.KV.Set(outKey, result.String(), 0); err != nil { return err }
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

// nextPC 推进 PC，不写结果。
func nextPC(f *op.Frame) {
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
}
