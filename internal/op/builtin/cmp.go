package builtin

import (
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type cmp struct{ f func(float64, float64) bool; s func(string, string) bool }
func (o cmp) Call(f *op.Frame) error {
	r, err := evalCmp(readInputs(f), o.f, o.s)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

// ── 比较 ──
func evalCmp(inputs []nativeValue, numCmp func(float64, float64) bool, strCmp func(string, string) bool) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil { return nativeValue{}, err }
	a, b := inputs[0], inputs[1]
	if (a.kind == "int" || a.kind == "float") && (b.kind == "int" || b.kind == "float") {
		return nativeValue{kind: "bool", b: numCmp(a.asFloat(), b.asFloat())}, nil
	}
	return nativeValue{kind: "bool", b: strCmp(a.raw, b.raw)}, nil
}
