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
