package builtin

import (
	"fmt"
	"math"
)

func evalAbs(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	v := inputs[0]
	switch v.kind {
	case "int":
		if v.i < 0 {
			return nativeValue{kind: "int", i: -v.i}, nil
		}
		return nativeValue{kind: "int", i: v.i}, nil
	case "float":
		return nativeValue{kind: "float", f: math.Abs(v.f)}, nil
	default:
		return nativeValue{}, fmt.Errorf("abs requires numeric, got %s", v.kind)
	}
}

func evalPow(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil {
		return nativeValue{}, err
	}
	return nativeValue{kind: "float", f: math.Pow(inputs[0].asFloat(), inputs[1].asFloat())}, nil
}

func evalMin(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil {
		return nativeValue{}, err
	}
	a, b := inputs[0], inputs[1]
	if (a.kind == "int" || a.kind == "float") && (b.kind == "int" || b.kind == "float") {
		af, bf := a.asFloat(), b.asFloat()
		result := math.Min(af, bf)
		if a.kind == "int" && b.kind == "int" {
			return nativeValue{kind: "int", i: int64(result)}, nil
		}
		return nativeValue{kind: "float", f: result}, nil
	}
	if a.raw < b.raw {
		return a, nil
	}
	return b, nil
}

func evalMax(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil {
		return nativeValue{}, err
	}
	a, b := inputs[0], inputs[1]
	if (a.kind == "int" || a.kind == "float") && (b.kind == "int" || b.kind == "float") {
		af, bf := a.asFloat(), b.asFloat()
		result := math.Max(af, bf)
		if a.kind == "int" && b.kind == "int" {
			return nativeValue{kind: "int", i: int64(result)}, nil
		}
		return nativeValue{kind: "float", f: result}, nil
	}
	if a.raw > b.raw {
		return a, nil
	}
	return b, nil
}

func evalSqrt(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	x := inputs[0].asFloat()
	if x < 0 {
		return nativeValue{}, fmt.Errorf("sqrt of negative number: %v", x)
	}
	return nativeValue{kind: "float", f: math.Sqrt(x)}, nil
}

func evalExp(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	return nativeValue{kind: "float", f: math.Exp(inputs[0].asFloat())}, nil
}

func evalLog(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	x := inputs[0].asFloat()
	if x <= 0 {
		return nativeValue{}, fmt.Errorf("log of non-positive number: %v", x)
	}
	return nativeValue{kind: "float", f: math.Log(x)}, nil
}

func evalSign(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	v := inputs[0]
	f := v.asFloat()
	if f > 0 {
		return nativeValue{kind: "int", i: 1}, nil
	} else if f < 0 {
		return nativeValue{kind: "int", i: -1}, nil
	}
	return nativeValue{kind: "int", i: 0}, nil
}
