package builtin

import (
	"fmt"
	"math"
	"strings"

	"kvlang/internal/logx"
)

func requireBinary(inputs []nativeValue) error {
	if len(inputs) != 2 { return fmt.Errorf("binary op requires 2 inputs, got %d", len(inputs)) }
	return nil
}
func requireUnary(inputs []nativeValue) error {
	if len(inputs) != 1 { return fmt.Errorf("unary op requires 1 input, got %d", len(inputs)) }
	return nil
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

// ── 比较 ──
func evalCmp(inputs []nativeValue, numCmp func(float64, float64) bool, strCmp func(string, string) bool) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	a, b := inputs[0], inputs[1]
	if (a.kind == "int" || a.kind == "float") && (b.kind == "int" || b.kind == "float") {
		return nativeValue{kind: "bool", b: numCmp(a.asFloat(), b.asFloat())}, nil
	}
	return nativeValue{kind: "bool", b: strCmp(a.raw, b.raw)}, nil
}

// ── 逻辑 ──
func evalLogic(inputs []nativeValue, fn func(bool, bool) bool) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "bool", b: fn(inputs[0].asBool(), inputs[1].asBool())}, nil
}
func evalNot(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "bool", b: !inputs[0].asBool()}, nil
}

// ── 位运算 ──
func evalBinaryInt(inputs []nativeValue, fn func(int64, int64) int64) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "int", i: fn(inputs[0].asInt(), inputs[1].asInt())}, nil
}

// ── 数学 ──
func evalAbs(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	v := inputs[0]
	switch v.kind {
	case "int": if v.i < 0 { return nativeValue{kind: "int", i: -v.i}, nil }; return nativeValue{kind: "int", i: v.i}, nil
	case "float": return nativeValue{kind: "float", f: math.Abs(v.f)}, nil
	default: return nativeValue{}, fmt.Errorf("abs requires numeric, got %s", v.kind)
	}
}
func evalPow(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "float", f: math.Pow(inputs[0].asFloat(), inputs[1].asFloat())}, nil
}
func evalMin(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	a, b := inputs[0], inputs[1]
	if (a.kind == "int" || a.kind == "float") && (b.kind == "int" || b.kind == "float") {
		af, bf := a.asFloat(), b.asFloat()
		result := math.Min(af, bf)
		if a.kind == "int" && b.kind == "int" { return nativeValue{kind: "int", i: int64(result)}, nil }
		return nativeValue{kind: "float", f: result}, nil
	}
	if a.raw < b.raw { return a, nil }
	return b, nil
}
func evalMax(inputs []nativeValue) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	a, b := inputs[0], inputs[1]
	if (a.kind == "int" || a.kind == "float") && (b.kind == "int" || b.kind == "float") {
		af, bf := a.asFloat(), b.asFloat()
		result := math.Max(af, bf)
		if a.kind == "int" && b.kind == "int" { return nativeValue{kind: "int", i: int64(result)}, nil }
		return nativeValue{kind: "float", f: result}, nil
	}
	if a.raw > b.raw { return a, nil }
	return b, nil
}
func evalSqrt(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	x := inputs[0].asFloat()
	if x < 0 { return nativeValue{}, fmt.Errorf("sqrt of negative number: %v", x) }
	return nativeValue{kind: "float", f: math.Sqrt(x)}, nil
}
func evalExp(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "float", f: math.Exp(inputs[0].asFloat())}, nil
}
func evalLog(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	x := inputs[0].asFloat()
	if x <= 0 { return nativeValue{}, fmt.Errorf("log of non-positive number: %v", x) }
	return nativeValue{kind: "float", f: math.Log(x)}, nil
}
func evalSign(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	f := inputs[0].asFloat()
	if f > 0 { return nativeValue{kind: "int", i: 1}, nil }
	if f < 0 { return nativeValue{kind: "int", i: -1}, nil }
	return nativeValue{kind: "int", i: 0}, nil
}

// ── 类型转换 ──
func evalToInt(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	v := inputs[0]
	switch v.kind {
	case "int": return v, nil
	case "float": return nativeValue{kind: "int", i: int64(v.f)}, nil
	case "bool": if v.b { return nativeValue{kind: "int", i: 1}, nil }; return nativeValue{kind: "int", i: 0}, nil
	default: return nativeValue{kind: "int", i: v.asInt()}, nil
	}
}
func evalToFloat(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	v := inputs[0]
	switch v.kind {
	case "float": return v, nil
	case "int": return nativeValue{kind: "float", f: float64(v.i)}, nil
	case "bool": if v.b { return nativeValue{kind: "float", f: 1.0}, nil }; return nativeValue{kind: "float", f: 0.0}, nil
	default: return nativeValue{kind: "float", f: v.asFloat()}, nil
	}
}
func evalToBool(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil { return nativeValue{}, err }
	return nativeValue{kind: "bool", b: inputs[0].asBool()}, nil
}

// ── IO ──
func evalPrint(inputs []nativeValue) (nativeValue, error) {
	if len(inputs) == 0 { return nativeValue{kind: "string", raw: ""}, nil }
	parts := make([]string, len(inputs))
	for i, v := range inputs { parts[i] = v.String() }
	logx.Debug("PRINT %s", strings.Join(parts, " "))
	return nativeValue{kind: "string", raw: strings.Join(parts, " ")}, nil
}
