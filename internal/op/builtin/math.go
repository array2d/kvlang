package builtin

import (
	"fmt"
	"math"

	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type mOp struct{ kind string }
func (o mOp) Call(f *op.Frame) error {
	r, err := evalMath(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalMath(kind string, inputs []kvspace.Value) (kvspace.Value, error) {
	switch kind {
	case "abs":  return evalAbs(inputs)
	case "pow":  return evalPow(inputs)
	case "min":  return evalMin(inputs)
	case "max":  return evalMax(inputs)
	case "sqrt": return evalSqrt(inputs)
	case "exp":  return evalExp(inputs)
	case "log":  return evalLog(inputs)
	case "neg":  return evalUnaryArith(inputs, func(a float64) float64 { return -a })
	case "sign": return evalSign(inputs)
	default:     return kvspace.Value{}, fmt.Errorf("unknown math: %s", kind)
	}
}

func evalAbs(inputs []kvspace.Value) (kvspace.Value, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.Value{}, err }
	v := inputs[0]
	switch v.Kind() {
	case "int":   if v.Int() < 0 { return kvspace.Int(-v.Int()), nil }; return v, nil
	case "float": return kvspace.Float(math.Abs(v.Float())), nil
	default:      return kvspace.Value{}, fmt.Errorf("abs requires numeric, got %s", v.Kind())
	}
}

func evalPow(inputs []kvspace.Value) (kvspace.Value, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.Value{}, err }
	return kvspace.Float(math.Pow(asFloat(inputs[0]), asFloat(inputs[1]))), nil
}

func evalMinMax(inputs []kvspace.Value, fn func(float64, float64) float64, strWin func(a, b kvspace.Value) kvspace.Value) (kvspace.Value, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.Value{}, err }
	a, b := inputs[0], inputs[1]
	if isNumeric(a) && isNumeric(b) {
		result := fn(asFloat(a), asFloat(b))
		if a.Kind() == "int" && b.Kind() == "int" { return kvspace.Int(int64(result)), nil }
		return kvspace.Float(result), nil
	}
	return strWin(a, b), nil
}

func evalMin(inputs []kvspace.Value) (kvspace.Value, error) {
	return evalMinMax(inputs, math.Min, func(a, b kvspace.Value) kvspace.Value {
		if a.Str() < b.Str() { return a }; return b
	})
}

func evalMax(inputs []kvspace.Value) (kvspace.Value, error) {
	return evalMinMax(inputs, math.Max, func(a, b kvspace.Value) kvspace.Value {
		if a.Str() > b.Str() { return a }; return b
	})
}

func evalSqrt(inputs []kvspace.Value) (kvspace.Value, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.Value{}, err }
	x := asFloat(inputs[0])
	if x < 0 { return kvspace.Value{}, fmt.Errorf("sqrt of negative number: %v", x) }
	return kvspace.Float(math.Sqrt(x)), nil
}

func evalExp(inputs []kvspace.Value) (kvspace.Value, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.Value{}, err }
	return kvspace.Float(math.Exp(asFloat(inputs[0]))), nil
}

func evalLog(inputs []kvspace.Value) (kvspace.Value, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.Value{}, err }
	x := asFloat(inputs[0])
	if x <= 0 { return kvspace.Value{}, fmt.Errorf("log of non-positive number: %v", x) }
	return kvspace.Float(math.Log(x)), nil
}

func evalSign(inputs []kvspace.Value) (kvspace.Value, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.Value{}, err }
	f := asFloat(inputs[0])
	if f > 0 { return kvspace.Int(1), nil }
	if f < 0 { return kvspace.Int(-1), nil }
	return kvspace.Int(0), nil
}
