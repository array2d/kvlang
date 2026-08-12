package builtin

import (
	"fmt"
	"math"

	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	Register("abs",  "rwir abs(A:num) -> (C:num)",        mOp{kind: "abs"})
	Register("pow",  "rwir pow(A:num, B:num) -> (C:float64)", mOp{kind: "pow"})
	Register("min",  "rwir min(A:num, B:num) -> (C:num)",     mOp{kind: "min"})
	Register("max",  "rwir max(A:num, B:num) -> (C:num)",     mOp{kind: "max"})
	registerWord("sqrt", "rwir sqrt(A:num) -> (C:float64)", mOp{kind: "sqrt"})
	Register("exp",  "rwir exp(A:num) -> (C:float64)",        mOp{kind: "exp"})
	Register("log",  "rwir log(A:num) -> (C:float64)",        mOp{kind: "log"})
	Register("neg",  "rwir neg(A:num) -> (C:num)",            mOp{kind: "neg"})
	Register("sign", "rwir sign(A:num) -> (C:int64)",         mOp{kind: "sign"})
}

type mOp struct{ kind string }
func (o mOp) Call(f *rwir.Frame) error {
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
	case "neg":
		if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
		return evalNeg(inputs[0])
	case "sign": return evalSign(inputs)
	default:     return kvspace.None{}, fmt.Errorf("unknown math: %s", kind)
	}
}

func evalAbs(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	v := inputs[0]
	switch v.Kind() {
	case "int8", "int16", "int32", "int64":
		x := asInt64(v)
		if x < 0 { return kvspace.NewInt64(-x), nil }
		return v, nil
	case "uint8", "uint16", "uint32", "uint64":
		return v, nil
	case "float32":
		return kvspace.NewFloat32(float32(math.Abs(float64(asFloat(v))))), nil
	case "float64":
		return kvspace.NewFloat64(math.Abs(asFloat(v))), nil
	default:
		return kvspace.None{}, fmt.Errorf("TypeError: abs requires numeric, got %s", v.Kind())
	}
}

func evalPow(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0], inputs[1]); err != nil { return kvspace.None{}, err }
	return kvspace.NewFloat64(math.Pow(asFloat(inputs[0]), asFloat(inputs[1]))), nil
}

func evalMinMax(inputs []kvspace.XValue, fn func(float64, float64) float64, intWin func(a, b int64) int64, strWin func(a, b string) string) (kvspace.XValue, error) {
	// 变参 fold（fix-026：Python/JS/Go 3/5 阵营支持 max(a,b,c...)，两参是少数派）
	if len(inputs) < 2 {
		return kvspace.None{}, fmt.Errorf("TypeError: min/max requires at least 2 inputs, got %d", len(inputs))
	}
	allInt, allNum, allStr := true, true, true
	for _, v := range inputs {
		if kvspace.IsNone(v) {
			return kvspace.None{}, fmt.Errorf("TypeError: None in min/max")
		}
		if !isIntKind(v.Kind()) { allInt = false }
		if !isNumeric(v) { allNum = false }
		if v.Kind() != "stringbyte" { allStr = false }
	}
	if !allNum && !allStr {
		return kvspace.None{}, fmt.Errorf("TypeError: min/max requires all numeric or all string")
	}
	// int 全域 → 原生 int64 fold（fix-020/022：不经 float64 中转）
	if allInt {
		acc := asInt64(inputs[0])
		for _, v := range inputs[1:] { acc = intWin(acc, asInt64(v)) }
		return kvspace.NewInt64(acc), nil
	}
	if allNum {
		acc := asFloat(inputs[0])
		for _, v := range inputs[1:] { acc = fn(acc, asFloat(v)) }
		return kvspace.NewFloat64(acc), nil
	}
	// allStr
	acc := inputs[0].ValueString()
	for _, v := range inputs[1:] { acc = strWin(acc, v.ValueString()) }
	return kvspace.NewStringByte([]byte(acc)...), nil
}

func evalMin(inputs []kvspace.XValue) (kvspace.XValue, error) {
	return evalMinMax(inputs, math.Min,
		func(a, b int64) int64 { if a < b { return a }; return b },
		func(a, b string) string { if a < b { return a }; return b })
}

func evalMax(inputs []kvspace.XValue) (kvspace.XValue, error) {
	return evalMinMax(inputs, math.Max,
		func(a, b int64) int64 { if a > b { return a }; return b },
		func(a, b string) string { if a > b { return a }; return b })
}

func evalSqrt(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0]); err != nil { return kvspace.None{}, err }
	x := asFloat(inputs[0])
	if x < 0 { return kvspace.None{}, fmt.Errorf("sqrt of negative number: %v", x) }
	return kvspace.NewFloat64(math.Sqrt(x)), nil
}

func evalExp(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0]); err != nil { return kvspace.None{}, err }
	return kvspace.NewFloat64(math.Exp(asFloat(inputs[0]))), nil
}

func evalLog(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0]); err != nil { return kvspace.None{}, err }
	x := asFloat(inputs[0])
	if x <= 0 { return kvspace.None{}, fmt.Errorf("ValueError: log of non-positive number: %v", x) }
	return kvspace.NewFloat64(math.Log(x)), nil
}

func evalSign(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0]); err != nil { return kvspace.None{}, err }
	f := asFloat(inputs[0])
	if f > 0 { return kvspace.NewInt64(1), nil }
	if f < 0 { return kvspace.NewInt64(-1), nil }
	return kvspace.NewInt64(0), nil
}
