package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type cOp struct{ kind string }
func (o cOp) Call(f *op.Frame) error {
	r, err := evalCast(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalCast(kind string, inputs []kvspace.XValue) (kvspace.XValue, error) {
	switch kind {
	case "int":   return evalToInt(inputs)
	case "float": return evalToFloat(inputs)
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

// castNum 数字类型算子公共路径：一元、nil 按 int 0（fix-017）、构造目标 kind。
func castNum(inputs []kvspace.XValue, mk func(kvspace.XValue) kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	return mk(nilAsInt(inputs[0])), nil
}

func evalToInt(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	v := inputs[0]
	if v.Kind() == "int" { return v, nil }
	return kvspace.Int(asInt(v)), nil
}

func evalToFloat(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	v := inputs[0]
	if v.Kind() == "float" { return v, nil }
	return kvspace.Float(asFloat(v)), nil
}

func evalToBool(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.XValue{}, err }
	return kvspace.Bool(AsBool(inputs[0])), nil
}
