package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	registerWord("add", "rwir add(A:num, B:num) -> (C:num)", arith{f: func(a, b float64) float64 { return a + b }, fi: func(a, b int64) int64 { return a + b }, concat: true})
	registerWord("sub", "rwir sub(A:num, B:num) -> (C:num)", arith{f: func(a, b float64) float64 { return a - b }, fi: func(a, b int64) int64 { return a - b }, unary: true})
	registerWord("mul", "rwir mul(A:num, B:num) -> (C:num)", arith{f: func(a, b float64) float64 { return a * b }, fi: func(a, b int64) int64 { return a * b }})
	registerWord("div", "rwir div(A:num, B:num) -> (C:num)", div{})
	registerWord("mod", "rwir mod(A:int64, B:int64) -> (C:int64)", mod{})
}

type arith struct{ f func(float64, float64) float64; fi func(int64, int64) int64; unary, concat bool }
func (o arith) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if o.unary && len(inputs) == 1 {
		r, err := evalNeg(inputs[0])
		if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
		return writeResult(f, r)
	}
	// + 号 string 拼接（fix-025：Python/JS/Go/Rust 4/5 阵营；C 无 + 拼接）
	if o.concat && len(inputs) == 2 && inputs[0].Kind() == "string" && inputs[1].Kind() == "string" {
		return writeResult(f, kvspace.String(inputs[0].Str()+inputs[1].Str()))
	}
	r, err := evalBinaryArith(inputs, o.f, o.fi)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

type div struct{}
func (div) Call(f *rwir.Frame) error {
	r, err := evalDiv(readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

type mod struct{}
func (mod) Call(f *rwir.Frame) error {
	r, err := evalMod(readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalBinaryArith(inputs []kvspace.XValue, fn func(float64, float64) float64, fnInt func(int64, int64) int64) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	a, b := inputs[0], inputs[1]
	if a.IsNone() || b.IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: None in arithmetic")
	}
	if err := requireNumeric(a, b); err != nil { return kvspace.XValue{}, err }
	// int ∧ int → 原生 int64 运算，绝不经 float64 中转（fix-020：
	// float64 仅 53 位尾数，>2^53 的整数会静默丢精度；溢出语义 = 补码回绕，同 C/Go）
	// 结果窄化回输入类型中更宽者，保留位宽信息（fix-034）。
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) {
		if fnInt == nil {
			panic("evalBinaryArith: fnInt is nil for int operands — every arith op must provide int path")
		}
		return narrowInt(a.Kind(), b.Kind(), fnInt(asInt(a), asInt(b))), nil
	}
	return narrowFloat(a.Kind(), b.Kind(), fn(asFloat(a), asFloat(b))), nil
}

func evalNeg(v kvspace.XValue) (kvspace.XValue, error) {
	if v.IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: None in arithmetic")
	}
	switch v.Kind() {
	case "int8", "int16", "int32", "int64":
		return kvspace.Int64(-v.Int64()), nil
	case "uint8", "uint16", "uint32", "uint64":
		return kvspace.XValue{}, fmt.Errorf("TypeError: cannot negate unsigned %s", v.Kind())
	case "float32":
		return kvspace.Float32(-v.Float32()), nil
	case "float64":
		return kvspace.Float64(-v.Float64()), nil
	default:
		return kvspace.XValue{}, fmt.Errorf("TypeError: cannot negate %s", v.Kind())
	}
}

func evalDiv(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	a, b := inputs[0], inputs[1]
	if a.IsNone() || b.IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: None in arithmetic")
	}
	if err := requireNumeric(a, b); err != nil { return kvspace.XValue{}, err }
	if asFloat(b) == 0 {
		return kvspace.XValue{}, fmt.Errorf("ZeroDivisionError: division by zero")
	}
	// 两整数 → 整数整除（C/Go/Rust 阵营）；含浮点 → 浮除。
	// fix-034：结果窄化回输入更宽类型，不复全部消灭为 int64/float64。
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) {
		return narrowInt(a.Kind(), b.Kind(), asInt(a)/asInt(b)), nil
	}
	return narrowFloat(a.Kind(), b.Kind(), asFloat(a)/asFloat(b)), nil
}

func evalMod(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	if inputs[0].IsNone() || inputs[1].IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: None in arithmetic")
	}
	// 模运算仅整数（五语言中仅 Python/JS 支持浮点取模；取 C/Go/Rust 阵营）
	if err := requireInt(inputs[0], inputs[1]); err != nil { return kvspace.XValue{}, err }
	b := asInt(inputs[1])
	if b == 0 { return kvspace.XValue{}, fmt.Errorf("ZeroDivisionError: modulo by zero") }
	return narrowInt(inputs[0].Kind(), inputs[1].Kind(), asInt(inputs[0])%b), nil
}

