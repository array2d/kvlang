package builtin

import (
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type cmp struct{ f func(float64, float64) bool; s func(string, string) bool }
func (o cmp) Call(f *op.Frame) error {
	r, err := evalCmp(readInputs(f), o.f, o.s)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}
