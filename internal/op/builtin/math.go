package builtin

import (
	"fmt"
	"math"

	"github.com/array2d/kvspace-go"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type mOp struct{ kind string }
func (o mOp) Call(f *op.Frame) error {
	r, err := evalMath(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalMath(kind string, inputs []kvspace.XValue) (kvspace.XValue, error) {
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
	default:     return kvspace.XValue{}, fmt.Errorf("unknown math: %s", kind)
	}
}

func evalAbs(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	v := inputs[0]
	switch v.Kind() {
	case "int", "int8", "int16", "int32", "int64":
		x := v.Int64()
		if x < 0 { return kvspace.Int64(-x), nil }
		return v, nil
	case "uint8", "uint16", "uint32", "uint64":
		return v, nil
	case "float", "float32":
		return kvspace.Float32(float32(math.Abs(float64(v.Float32())))), nil
	case "float64":
		return kvspace.Float64(math.Abs(v.Float64())), nil
	default:
		return kvspace.XValue{}, fmt.Errorf("TypeError: abs requires numeric, got %s", v.Kind())
	}
}

func evalPow(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	return kvspace.Float(math.Pow(asFloat(inputs[0]), asFloat(inputs[1]))), nil
}

func evalMinMax(inputs []kvspace.XValue, fn func(float64, float64) float64, intWin func(a, b int64) int64, strWin func(a, b kvspace.XValue) kvspace.XValue) (kvspace.XValue, error) {
	// 变参 fold（fix-026：Python/JS/Go 3/5 阵营支持 max(a,b,c...)，两参是少数派）
	if len(inputs) < 2 {
		return kvspace.XValue{}, fmt.Errorf("TypeError: min/max requires at least 2 inputs, got %d", len(inputs))
	}
	vals := make([]kvspace.XValue, len(inputs))
	allInt, allNum := true, true
	for i, v := range inputs {
		vals[i] = nilAsInt(v)
		if !isIntKind(vals[i].Kind()) { allInt = false }
		if !isNumeric(vals[i]) { allNum = false }
	}
	// int 全域 → 原生 int64 fold（fix-020/022：不经 float64 中转）
	if allInt {
		acc := asInt(vals[0])
		for _, v := range vals[1:] { acc = intWin(acc, asInt(v)) }
		return kvspace.Int(acc), nil
	}
	if allNum {
		acc := asFloat(vals[0])
		for _, v := range vals[1:] { acc = fn(acc, asFloat(v)) }
		return kvspace.Float(acc), nil
	}
	acc := vals[0]
	for _, v := range vals[1:] { acc = strWin(acc, v) }
	return acc, nil
}

func evalMin(inputs []kvspace.XValue) (kvspace.XValue, error) {
	return evalMinMax(inputs, math.Min,
		func(a, b int64) int64 { if a < b { return a }; return b },
		func(a, b kvspace.XValue) kvspace.XValue {
			if a.Str() < b.Str() { return a }; return b
		})
}

func evalMax(inputs []kvspace.XValue) (kvspace.XValue, error) {
	return evalMinMax(inputs, math.Max,
		func(a, b int64) int64 { if a > b { return a }; return b },
		func(a, b kvspace.XValue) kvspace.XValue {
			if a.Str() > b.Str() { return a }; return b
		})
}

func evalSqrt(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	x := asFloat(inputs[0])
	if x < 0 { return kvspace.XValue{}, fmt.Errorf("sqrt of negative number: %v", x) }
	return kvspace.Float(math.Sqrt(x)), nil
}

func evalExp(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	return kvspace.Float(math.Exp(asFloat(inputs[0]))), nil
}

func evalLog(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	x := asFloat(inputs[0])
	if x <= 0 { return kvspace.XValue{}, fmt.Errorf("ValueError: log of non-positive number: %v", x) }
	return kvspace.Float(math.Log(x)), nil
}

func evalSign(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	f := asFloat(inputs[0])
	if f > 0 { return kvspace.Int(1), nil }
	if f < 0 { return kvspace.Int(-1), nil }
	return kvspace.Int(0), nil
}
