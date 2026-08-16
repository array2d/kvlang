package builtin

import (
	"fmt"
	"math"

	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	registerKinds("pow", 2, "float64", floatKinds, mOp{kind: "pow"})
	registerKinds("sqrt", 1, "float64", floatKinds, mOp{kind: "sqrt"})
	registerKinds("exp", 1, "float64", floatKinds, mOp{kind: "exp"})
	registerKinds("log", 1, "float64", floatKinds, mOp{kind: "log"})
	registerKinds("neg", 1, "", signedOrFloatKinds, mOp{kind: "neg"})
	registerKinds("abs", 1, "", signedOrFloatKinds, mOp{kind: "abs"})
	registerKinds("sign", 1, "int64", numKinds, mOp{kind: "sign"})
	registerKinds("max", 2, "", numKinds, mOp{kind: "max"})
	registerKinds("min", 2, "", numKinds, mOp{kind: "min"})
}

type mOp struct{ kind string }
func (o mOp) Call(f *rwir.Frame) error {
	r, err := evalMath(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalMath(kind string, inputs []kvspace.XValue) (kvspace.XValue, error) {
	switch kind {
	case "pow":  return evalPow(inputs)
	case "sqrt": return evalSqrt(inputs)
	case "exp":  return evalExp(inputs)
	case "log":  return evalLog(inputs)
	case "neg":
		if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
		return evalNeg(inputs[0])
	case "abs":
		if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
		return evalAbs(inputs[0])
	case "sign":
		if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
		return evalSign(inputs[0])
	case "max":
		return evalMaxMin(inputs, true)
	case "min":
		return evalMaxMin(inputs, false)
	default:     return kvspace.None{}, fmt.Errorf("unknown math: %s", kind)
	}
}

func evalAbs(v kvspace.XValue) (kvspace.XValue, error) {
	if isIntKind(v.Kind()) {
		iv := asInt64(v)
		if iv < 0 {
			iv = -iv
		}
		return narrowInt(v.Kind(), v.Kind(), iv), nil
	}
	if isFloatKind(v.Kind()) {
		return narrowFloat(v.Kind(), v.Kind(), math.Abs(asFloat(v))), nil
	}
	return kvspace.None{}, fmt.Errorf("TypeError: abs requires numeric, got %s", v.Kind())
}

func evalSign(v kvspace.XValue) (kvspace.XValue, error) {
	if isIntKind(v.Kind()) {
		iv := asInt64(v)
		switch {
		case iv < 0:
			return kvspace.NewInt64(-1), nil
		case iv > 0:
			return kvspace.NewInt64(1), nil
		default:
			return kvspace.NewInt64(0), nil
		}
	}
	if isFloatKind(v.Kind()) {
		fv := asFloat(v)
		switch {
		case fv < 0:
			return kvspace.NewInt64(-1), nil
		case fv > 0:
			return kvspace.NewInt64(1), nil
		default:
			return kvspace.NewInt64(0), nil
		}
	}
	return kvspace.None{}, fmt.Errorf("TypeError: sign requires numeric, got %s", v.Kind())
}

func evalMaxMin(inputs []kvspace.XValue, isMax bool) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil {
		return kvspace.None{}, err
	}
	a, b := inputs[0], inputs[1]
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) {
		c := cmpInt(a, b)
		if (isMax && c >= 0) || (!isMax && c <= 0) {
			return narrowInt(a.Kind(), b.Kind(), asInt64(a)), nil
		}
		return narrowInt(a.Kind(), b.Kind(), asInt64(b)), nil
	}
	if isNumeric(a) && isNumeric(b) {
		fa, fb := asFloat(a), asFloat(b)
		if (isMax && fa >= fb) || (!isMax && fa <= fb) {
			return narrowFloat(a.Kind(), b.Kind(), fa), nil
		}
		return narrowFloat(a.Kind(), b.Kind(), fb), nil
	}
	return kvspace.None{}, fmt.Errorf("TypeError: max/min requires numeric, got %s and %s", a.Kind(), b.Kind())
}

func evalPow(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0], inputs[1]); err != nil { return kvspace.None{}, err }
	return kvspace.NewFloat64(math.Pow(asFloat(inputs[0]), asFloat(inputs[1]))), nil
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
