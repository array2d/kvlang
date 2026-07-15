package builtin

import (
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type logic struct{ f func(bool, bool) bool }
func (o logic) Call(f *op.Frame) error {
	r, err := evalLogic(readInputs(f), o.f)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

type not struct{}
func (not) Call(f *op.Frame) error {
	r, err := evalNot(readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalLogic(inputs []kvspace.Value, fn func(bool, bool) bool) (kvspace.Value, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.Value{}, err }
	return kvspace.Bool(fn(AsBool(inputs[0]), AsBool(inputs[1]))), nil
}

func evalNot(inputs []kvspace.Value) (kvspace.Value, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.Value{}, err }
	return kvspace.Bool(!AsBool(inputs[0])), nil
}
