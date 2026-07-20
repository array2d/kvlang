package builtin

import (
	"github.com/array2d/kvspace-go"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type cmp struct {
	f func(float64, float64) bool
	i func(int64, int64) bool
	s func(string, string) bool
}
func (o cmp) Call(f *op.Frame) error {
	r, err := evalCmp(readInputs(f), o.f, o.i, o.s)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalCmp(inputs []kvspace.XValue, numCmp func(float64, float64) bool, intCmp func(int64, int64) bool, strCmp func(string, string) bool) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	a, b := inputs[0], inputs[1]
	// int ∧ int → 原生 int64 比较（fix-020：>2^53 经 float64 会误判相等）
	if isIntKind(a.Kind()) && isIntKind(b.Kind()) && intCmp != nil {
		return kvspace.Bool(intCmp(asInt(a), asInt(b))), nil
	}
	// 混合数值 → float64 提升比较（C 阵营语义：3 == 3.0 为 true；
	// 大整数 vs float 的混合比较存在 float64 固有精度边界，与 C 一致）
	if isNumeric(a) && isNumeric(b) {
		return kvspace.Bool(numCmp(asFloat(a), asFloat(b))), nil
	}
	return kvspace.Bool(strCmp(a.Str(), b.Str())), nil
}
