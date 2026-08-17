package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"oldhero/rwir"
	"oldhero/vthread"
)

func init() {
	Register("bool",    "rwir bool(A:any) -> (C:bool)",       cOp{kind: "bool"})
	Register("int8",    "rwir int8(A:any) -> (C:int8)",       cOp{kind: "int8"})
	Register("int16",   "rwir int16(A:any) -> (C:int16)",     cOp{kind: "int16"})
	Register("int32",   "rwir int32(A:any) -> (C:int32)",     cOp{kind: "int32"})
	Register("int64",   "rwir int64(A:any) -> (C:int64)",     cOp{kind: "int64"})
	Register("uint8",   "rwir uint8(A:any) -> (C:uint8)",     cOp{kind: "uint8"})
	Register("uint16",  "rwir uint16(A:any) -> (C:uint16)",   cOp{kind: "uint16"})
	Register("uint32",  "rwir uint32(A:any) -> (C:uint32)",   cOp{kind: "uint32"})
	Register("uint64",  "rwir uint64(A:any) -> (C:uint64)",   cOp{kind: "uint64"})
	Register("float32", "rwir float32(A:any) -> (C:float32)", cOp{kind: "float32"})
	Register("float64", "rwir float64(A:any) -> (C:float64)", cOp{kind: "float64"})
	Register("char/utf32", "rwir char/utf32(A:any) -> (C:char/utf32)", cCharOp{kind: kvspace.KindChar})
	Register("char/utf8",  "rwir char/utf8(A:any) -> (C:char/utf8)",   cCharOp{kind: kvspace.KindCharUtf8})
	Register("char/ascii", "rwir char/ascii(A:any) -> (C:char/ascii)", cCharOp{kind: kvspace.KindCharAscii})
}

type cOp struct{ kind string }
func (o cOp) Call(f *rwir.Frame) error {
	r, err := evalCast(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

// cCharOp 字符编码转换/创建：NewChar(kind, s) 同时承载创建与转换（同创建函数）。
type cCharOp struct{ kind string }
func (o cCharOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) == 0 || kvspace.IsNone(inputs[0]) {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: char conversion requires a value")
		return fmt.Errorf("TypeError: char conversion requires a value")
	}
	if o.kind == kvspace.KindCharAscii {
		runes := stringRunes(inputs[0])
		data := make([]uint8, len(runes))
		for i, r := range runes {
			if r > 127 {
				msg := fmt.Sprintf("TypeError: char.ascii: non-ASCII code point U+%04X", r)
				vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
				return fmt.Errorf("%s", msg)
			}
			data[i] = uint8(r)
		}
		return writeResult(f, kvspace.NewCharAscii(data...))
	}
	return writeResult(f, kvspace.NewChar(o.kind, inputs[0].ValueString()))
}

func evalCast(kind string, inputs []kvspace.XValue) (kvspace.XValue, error) {
	switch kind {
	case "bool":  return evalToBool(inputs)
	// 全谱数字类型创建/转换（fix-021）：float→int 截断向零，窄化=补码回绕（同 Go/Rust as/C 转换）
	case "int8":    return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewInt8(int8(asInt64(v))) })
	case "int16":   return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewInt16(int16(asInt64(v))) })
	case "int32":   return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewInt32(int32(asInt64(v))) })
	case "int64":   return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewInt64(asInt64(v)) })
	case "uint8":   return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewUint8(uint8(asInt64(v))) })
	case "uint16":  return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewUint16(uint16(asInt64(v))) })
	case "uint32":  return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewUint32(uint32(asInt64(v))) })
	case "uint64":  return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewUint64(uint64(asInt64(v))) })
	case "float32": return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewFloat32(float32(asFloat(v))) })
	case "float64": return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.NewFloat64(asFloat(v)) })
	default:      return kvspace.None{}, fmt.Errorf("unknown cast: %s", kind)
	}
}

// castNum 数字类型算子公共路径：一元、检查 nil、构造目标 kind。
func castNum(inputs []kvspace.XValue, mk func(kvspace.XValue) kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if kvspace.IsNone(inputs[0]) {
		return kvspace.None{}, fmt.Errorf("TypeError: cannot cast None")
	}
	return mk(inputs[0]), nil
}

func evalToBool(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if kvspace.IsNone(inputs[0]) {
		return kvspace.None{}, fmt.Errorf("TypeError: cannot cast None")
	}
	return kvspace.NewBool(AsBool(inputs[0])), nil
}
