package builtin

import (
	"fmt"

	"kvlang/internal/kvspace"
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
	r, err := evalDiv(readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

type mod struct{}
func (mod) Call(f *op.Frame) error {
	r, err := evalMod(readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalBinaryArith(inputs []kvspace.XValue, fn func(float64, float64) float64) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	a, b := inputs[0], inputs[1]
	result := fn(asFloat(a), asFloat(b))
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) { return kvspace.Int64(int64(result)), nil }
	return kvspace.Float(result), nil
}

func evalNeg(v kvspace.XValue) (kvspace.XValue, error) {
	switch v.Kind() {
	case "int":   return kvspace.Int(-v.Int()), nil
	case "float": return kvspace.Float(-v.Float()), nil
	default:      return kvspace.XValue{}, fmt.Errorf("cannot negate %s", v.Kind())
	}
}

func evalDiv(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	bf := asFloat(inputs[1])
	if bf == 0 { return kvspace.XValue{}, fmt.Errorf("division by zero") }
	return kvspace.Float(asFloat(inputs[0]) / bf), nil
}

func evalMod(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	b := asInt(inputs[1])
	if b == 0 { return kvspace.XValue{}, fmt.Errorf("modulo by zero") }
	return kvspace.Int(asInt(inputs[0]) % b), nil
}

func evalUnaryArith(inputs []kvspace.XValue, fn func(float64) float64) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	v := inputs[0]
	result := fn(asFloat(v))
	if isIntKind(v.Kind()) { return kvspace.Int(int64(result)), nil }
	return kvspace.Float(result), nil
}
