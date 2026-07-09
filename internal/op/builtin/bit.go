package builtin

import (
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type bit struct{ f func(int64, int64) int64 }
func (o bit) Call(f *op.Frame) error {
	r, err := evalBinaryInt(readInputs(f), o.f)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

// ── 位运算 ──
func evalBinaryInt(inputs []nativeValue, fn func(int64, int64) int64) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "int", i: fn(inputs[0].asInt(), inputs[1].asInt())}, nil
}
