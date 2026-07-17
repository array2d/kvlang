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
	case "int", "int8", "int16", "int32", "int64":
		return kvspace.Int64(-v.Int64()), nil
	case "uint8", "uint16", "uint32", "uint64":
		return kvspace.XValue{}, fmt.Errorf("cannot negate unsigned %s", v.Kind())
	case "float", "float32":
		return kvspace.Float32(-v.Float32()), nil
	case "float64":
		return kvspace.Float64(-v.Float64()), nil
	default:
		return kvspace.XValue{}, fmt.Errorf("cannot negate %s", v.Kind())
	}
}

func evalDiv(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	bf := asFloat(inputs[1])
	if bf == 0 { return kvspace.XValue{}, fmt.Errorf("division by zero") }
	// C 风格：两整数相除 → 整除，否则 → 浮除
	a, b := inputs[0], inputs[1]
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) {
		return kvspace.Int64(asInt(a) / asInt(b)), nil
	}
	return kvspace.Float64(asFloat(a) / bf), nil
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

