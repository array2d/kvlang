package builtin

import (
	"github.com/array2d/kvlang-go"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type cmp struct{ f func(float64, float64) bool; s func(string, string) bool }
func (o cmp) Call(f *op.Frame) error {
	r, err := evalCmp(readInputs(f), o.f, o.s)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalCmp(inputs []kvspace.XValue, numCmp func(float64, float64) bool, strCmp func(string, string) bool) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	a, b := inputs[0], inputs[1]
	if isNumeric(a) && isNumeric(b) {
		return kvspace.Bool(numCmp(asFloat(a), asFloat(b))), nil
	}
	return kvspace.Bool(strCmp(a.Str(), b.Str())), nil
}
