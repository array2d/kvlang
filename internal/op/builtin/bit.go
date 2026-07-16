package builtin

import (
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type bit struct{ f func(int64, int64) int64 }
func (o bit) Call(f *op.Frame) error {
	r, err := evalBinaryInt(readInputs(f), o.f)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalBinaryInt(inputs []kvspace.XValue, fn func(int64, int64) int64) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	return kvspace.Int(fn(asInt(inputs[0]), asInt(inputs[1]))), nil
}
