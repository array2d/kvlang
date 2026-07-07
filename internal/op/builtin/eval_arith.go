package builtin

import "fmt"

func evalBinaryArith(inputs []nativeValue, fn func(float64, float64) float64) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil {
		return nativeValue{}, err
	}
	a, b := inputs[0], inputs[1]
	result := fn(a.asFloat(), b.asFloat())
	if a.kind == "int" && b.kind == "int" {
		return nativeValue{kind: "int", i: int64(result)}, nil
	}
	return nativeValue{kind: "float", f: result}, nil
}

func evalNeg(v nativeValue) (nativeValue, error) {
	switch v.kind {
	case "int":
		return nativeValue{kind: "int", i: -v.i}, nil
	case "float":
		return nativeValue{kind: "float", f: -v.f}, nil
	default:
		return nativeValue{}, fmt.Errorf("cannot negate %s", v.kind)
	}
}

func evalDiv(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil {
		return nativeValue{}, err
	}
	a, b := inputs[0], inputs[1]
	bf := b.asFloat()
	if bf == 0 {
		return nativeValue{}, fmt.Errorf("division by zero")
	}
	return nativeValue{kind: "float", f: a.asFloat() / bf}, nil
}

func evalMod(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil {
		return nativeValue{}, err
	}
	a, b := inputs[0], inputs[1]
	if b.asInt() == 0 {
		return nativeValue{}, fmt.Errorf("modulo by zero")
	}
	return nativeValue{kind: "int", i: a.asInt() % b.asInt()}, nil
}

func evalUnaryArith(inputs []nativeValue, fn func(float64) float64) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	v := inputs[0]
	result := fn(v.asFloat())
	if v.kind == "int" {
		return nativeValue{kind: "int", i: int64(result)}, nil
	}
	return nativeValue{kind: "float", f: result}, nil
}
