package builtin

import (
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type logic struct{ f func(bool, bool) bool }
func (o logic) Call(f *op.Frame) error {
	r, err := evalLogic(readInputs(f), o.f)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

type not struct{}
func (not) Call(f *op.Frame) error {
	r, err := evalNot(readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

// ── 逻辑 ──
func evalLogic(inputs []nativeValue, fn func(bool, bool) bool) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "bool", b: fn(inputs[0].asBool(), inputs[1].asBool())}, nil
}
func evalNot(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "bool", b: !inputs[0].asBool()}, nil
}
