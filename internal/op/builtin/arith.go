package builtin

import (
	"fmt"

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

// ── 算术 ──
func evalBinaryArith(inputs []nativeValue, fn func(float64, float64) float64) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	a, b := inputs[0], inputs[1]
	result := fn(a.asFloat(), b.asFloat())
	if a.kind == "int" && b.kind == "int" { return nativeValue{kind: "int", i: int64(result)}, nil }
	return nativeValue{kind: "float", f: result}, nil
}
func evalNeg(v nativeValue) (nativeValue, error) {
	switch v.kind {
	case "int": return nativeValue{kind: "int", i: -v.i}, nil
	case "float": return nativeValue{kind: "float", f: -v.f}, nil
	default: return nativeValue{}, fmt.Errorf("cannot negate %s", v.kind)
	}
}
func evalDiv(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	a, b := inputs[0], inputs[1]
	bf := b.asFloat()
	if bf == 0 { return nativeValue{}, fmt.Errorf("division by zero") }
	return nativeValue{kind: "float", f: a.asFloat() / bf}, nil
}
func evalMod(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	a, b := inputs[0], inputs[1]
	if b.asInt() == 0 { return nativeValue{}, fmt.Errorf("modulo by zero") }
	return nativeValue{kind: "int", i: a.asInt() % b.asInt()}, nil
}
func evalUnaryArith(inputs []nativeValue, fn func(float64) float64) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	v := inputs[0]
	result := fn(v.asFloat())
	if v.kind == "int" { return nativeValue{kind: "int", i: int64(result)}, nil }
	return nativeValue{kind: "float", f: result}, nil
}
