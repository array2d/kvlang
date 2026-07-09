package builtin

import (
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type arith struct{ f func(float64, float64) float64; unary bool }
func (o arith) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if o.unary && len(inputs) == 1 {
		r, err := evalNeg(inputs[0])
		if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
		return writeResult(f, r)
	}
	r, err := evalBinaryArith(inputs, o.f)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

type div struct{}
func (div) Call(f *op.Frame) error {
	inputs := readInputs(f)
	r, err := evalDiv(inputs)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

type mod struct{}
func (mod) Call(f *op.Frame) error {
	inputs := readInputs(f)
	r, err := evalMod(inputs)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}
