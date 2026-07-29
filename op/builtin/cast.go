package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"kvlang/op"
	"kvlang/vthread"
)

func init() {
	Register("bool",    "def bool(A:any) -> (C:bool)",       cOp{kind: "bool"})
	Register("int8",    "def int8(A:any) -> (C:int8)",       cOp{kind: "int8"})
	Register("int16",   "def int16(A:any) -> (C:int16)",     cOp{kind: "int16"})
	Register("int32",   "def int32(A:any) -> (C:int32)",     cOp{kind: "int32"})
	Register("int64",   "def int64(A:any) -> (C:int64)",     cOp{kind: "int64"})
	Register("uint8",   "def uint8(A:any) -> (C:uint8)",     cOp{kind: "uint8"})
	Register("uint16",  "def uint16(A:any) -> (C:uint16)",   cOp{kind: "uint16"})
	Register("uint32",  "def uint32(A:any) -> (C:uint32)",   cOp{kind: "uint32"})
	Register("uint64",  "def uint64(A:any) -> (C:uint64)",   cOp{kind: "uint64"})
	Register("float32", "def float32(A:any) -> (C:float32)", cOp{kind: "float32"})
	Register("float64", "def float64(A:any) -> (C:float64)", cOp{kind: "float64"})
}

type cOp struct{ kind string }
func (o cOp) Call(f *op.Frame) error {
	r, err := evalCast(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalCast(kind string, inputs []kvspace.XValue) (kvspace.XValue, error) {
	switch kind {
	case "bool":  return evalToBool(inputs)
	// 全谱数字类型创建/转换（fix-021）：float→int 截断向零，窄化=补码回绕（同 Go/Rust as/C 转换）
	case "int8":    return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Int8(int8(asInt(v))) })
	case "int16":   return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Int16(int16(asInt(v))) })
	case "int32":   return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Int32(int32(asInt(v))) })
	case "int64":   return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Int64(asInt(v)) })
	case "uint8":   return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Uint8(uint8(asInt(v))) })
	case "uint16":  return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Uint16(uint16(asInt(v))) })
	case "uint32":  return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Uint32(uint32(asInt(v))) })
	case "uint64":  return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Uint64(uint64(asInt(v))) })
	case "float32": return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Float32(float32(asFloat(v))) })
	case "float64": return castNum(inputs, func(v kvspace.XValue) kvspace.XValue { return kvspace.Float64(asFloat(v)) })
	default:      return kvspace.XValue{}, fmt.Errorf("unknown cast: %s", kind)
	}
}

// castNum 数字类型算子公共路径：一元、检查 nil、构造目标 kind。
func castNum(inputs []kvspace.XValue, mk func(kvspace.XValue) kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	if inputs[0].IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: cannot cast None")
	}
	return mk(inputs[0]), nil
}

func evalToBool(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	if inputs[0].IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: cannot cast None")
	}
	return kvspace.Bool(AsBool(inputs[0])), nil
}
