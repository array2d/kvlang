package builtin

import "fmt"

import (
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type cOp struct{ kind string }
func (o cOp) Call(f *op.Frame) error {
	r, err := evalCast(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalCast(kind string, inputs []nativeValue) (nativeValue, error) {
	switch kind {
	case "int": return evalToInt(inputs)
	case "float": return evalToFloat(inputs)
	case "bool": return evalToBool(inputs)
	default: return nativeValue{}, fmt.Errorf("unknown cast: %s", kind)
	}
}
