package builtin

import (
	"fmt"

	"kvlang/internal/kvspace"
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
	default:      return kvspace.XValue{}, fmt.Errorf("unknown cast: %s", kind)
	}
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
