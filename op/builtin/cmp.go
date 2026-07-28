package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"kvlang/op"
	"kvlang/vthread"
)

type cmp struct {
	f         func(float64, float64) bool
	i         func(int64, int64) bool
	s         func(string, string) bool
	allowNull bool // ==/!= 允许 null kind 比较；< > <= >= 不允许
}
func (o cmp) Call(f *op.Frame) error {
	r, err := evalCmp(readInputs(f), o)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalCmp(inputs []kvspace.XValue, o cmp) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	a, b := inputs[0], inputs[1]

	// null kind 比较：仅 ==/!= 允许，直接比较 kind
	if a.IsNone() || b.IsNone() {
		if !o.allowNull {
			return kvspace.XValue{}, fmt.Errorf("TypeError: null in comparison")
		}
		return kvspace.Bool(o.s(a.Kind(), b.Kind())), nil
	}

	if isIntKind(a.Kind()) && isIntKind(b.Kind()) && o.i != nil {
		return kvspace.Bool(o.i(asInt(a), asInt(b))), nil
	}
	if isNumeric(a) && isNumeric(b) {
		return kvspace.Bool(o.f(asFloat(a), asFloat(b))), nil
	}
	return kvspace.Bool(o.s(a.Str(), b.Str())), nil
}
