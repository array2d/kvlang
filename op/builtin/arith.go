package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"kvlang/op"
	"kvlang/vthread"
)

type arith struct{ f func(float64, float64) float64; fi func(int64, int64) int64; unary, concat bool }
func (o arith) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if o.unary && len(inputs) == 1 {
		r, err := evalNeg(inputs[0])
		if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
		return writeResult(f, r)
	}
	// + 号 string 拼接（fix-025：Python/JS/Go/Rust 4/5 阵营；C 无 + 拼接）
	if o.concat && len(inputs) == 2 && inputs[0].Kind() == "string" && inputs[1].Kind() == "string" {
		return writeResult(f, kvspace.Str(inputs[0].Str()+inputs[1].Str()))
	}
	r, err := evalBinaryArith(inputs, o.f, o.fi)
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

func evalBinaryArith(inputs []kvspace.XValue, fn func(float64, float64) float64, fnInt func(int64, int64) int64) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	if inputs[0].IsNone() || inputs[1].IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: null in arithmetic")
	}
	a, b := inputs[0], inputs[1]
	// int ∧ int → 原生 int64 运算，绝不经 float64 中转（fix-020：
	// float64 仅 53 位尾数，>2^53 的整数会静默丢精度；溢出语义 = 补码回绕，同 C/Go）
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) && fnInt != nil {
		return kvspace.Int64(fnInt(asInt(a), asInt(b))), nil
	}
	result := fn(asFloat(a), asFloat(b))
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) { return kvspace.Int64(int64(result)), nil }
	return kvspace.Float64(result), nil
}

func evalNeg(v kvspace.XValue) (kvspace.XValue, error) {
	if v.IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: null in arithmetic")
	}
	switch v.Kind() {
	case "int", "int8", "int16", "int32", "int64":
		return kvspace.Int64(-v.Int64()), nil
	case "uint8", "uint16", "uint32", "uint64":
		return kvspace.XValue{}, fmt.Errorf("TypeError: cannot negate unsigned %s", v.Kind())
	case "float", "float32":
		return kvspace.Float32(-v.Float32()), nil
	case "float64":
		return kvspace.Float64(-v.Float64()), nil
	default:
		return kvspace.XValue{}, fmt.Errorf("TypeError: cannot negate %s", v.Kind())
	}
}

func evalDiv(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	if inputs[0].IsNone() || inputs[1].IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: null in arithmetic")
	}
	bf := asFloat(inputs[1])
	// fix-rc1：同时检 asFloat 与 asInt。非常小的浮点（如 1e-20）的 asFloat!=0
	// 但 asInt 截断为零 → 整数路径 Go panic，浮点路径也产生反常大数。
	if bf == 0 || asInt(inputs[1]) == 0 {
		return kvspace.XValue{}, fmt.Errorf("ZeroDivisionError: division by zero")
	}
	// C 风格：两整数相除 → 整除，否则 → 浮除
	a, b := inputs[0], inputs[1]
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) {
		return kvspace.Int64(asInt(a) / asInt(b)), nil
	}
	return kvspace.Float64(asFloat(a) / bf), nil
}

func evalMod(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	if inputs[0].IsNone() || inputs[1].IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: null in arithmetic")
	}
	b := asInt(inputs[1])
	if b == 0 { return kvspace.XValue{}, fmt.Errorf("ZeroDivisionError: modulo by zero") }
	return kvspace.Int64(asInt(inputs[0]) % b), nil
}

func evalUnaryArith(inputs []kvspace.XValue, fn func(float64) float64) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	if inputs[0].IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: null in arithmetic")
	}
	v := inputs[0]
	result := fn(asFloat(v))
	if isIntKind(v.Kind()) { return kvspace.Int64(int64(result)), nil }
	return kvspace.Float64(result), nil
}

