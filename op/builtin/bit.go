package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"kvlang/op"
	"kvlang/vthread"
)

type bit struct{ f func(int64, int64) int64 }
func (o bit) Call(f *op.Frame) error {
	r, err := evalBinaryInt(readInputs(f), o.f)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalBinaryInt(inputs []kvspace.XValue, fn func(int64, int64) int64) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	if inputs[0].IsNone() || inputs[1].IsNone() {
		return kvspace.XValue{}, fmt.Errorf("TypeError: None in bitwise operation")
	}
	return kvspace.Int64(fn(asInt(inputs[0]), asInt(inputs[1]))), nil
}
