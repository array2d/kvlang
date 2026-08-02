package builtin

import (
	"fmt"
	"strconv"

	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	eqCmp := cmp{f: func(a, b float64) bool { return a == b }, i: func(a, b int64) bool { return a == b }, s: func(a, b string) bool { return a == b }, allowNull: true}
	registerWord("eq",  "rwir eq(A:num, B:num) -> (C:bool)",  eqCmp)
	neqCmp := cmp{f: func(a, b float64) bool { return a != b }, i: func(a, b int64) bool { return a != b }, s: func(a, b string) bool { return a != b }, allowNull: true}
	registerWord("neq", "rwir neq(A:num, B:num) -> (C:bool)", neqCmp)
	registerWord("lt",  "rwir lt(A:num, B:num) -> (C:bool)",  cmp{f: func(a, b float64) bool { return a < b },  i: func(a, b int64) bool { return a < b },  s: func(a, b string) bool { return a < b }})
	registerWord("gt",  "rwir gt(A:num, B:num) -> (C:bool)",  cmp{f: func(a, b float64) bool { return a > b },  i: func(a, b int64) bool { return a > b },  s: func(a, b string) bool { return a > b }})
	registerWord("le",  "rwir le(A:num, B:num) -> (C:bool)",  cmp{f: func(a, b float64) bool { return a <= b }, i: func(a, b int64) bool { return a <= b }, s: func(a, b string) bool { return a <= b }})
	registerWord("ge",  "rwir ge(A:num, B:num) -> (C:bool)",  cmp{f: func(a, b float64) bool { return a >= b }, i: func(a, b int64) bool { return a >= b }, s: func(a, b string) bool { return a >= b }})
}

type cmp struct {
	f         func(float64, float64) bool
	i         func(int64, int64) bool
	s         func(string, string) bool
	allowNull bool // ==/!= 允许 null kind 比较；< > <= >= 不允许
}
func (o cmp) Call(f *rwir.Frame) error {
	r, err := evalCmp(readInputs(f), o)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalCmp(inputs []kvspace.XValue, o cmp) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.None{}, err }
	a, b := inputs[0], inputs[1]

	// null kind 比较：仅 ==/!= 允许
	if kvspace.IsNone(a) || kvspace.IsNone(b) {
		if !o.allowNull {
			return kvspace.None{}, fmt.Errorf("TypeError: None in comparison")
		}
		return kvspace.NewBool(o.s(a.Kind(), b.Kind())), nil
	}

	// 同类型直接比较；混合类型 → TypeError（p0/p1：不容忍跨类型静默比较）
	switch {
	case isIntKind(a.Kind()) && isIntKind(b.Kind()) && o.i != nil:
		return kvspace.NewBool(o.i(asInt(a), asInt(b))), nil
	case isNumeric(a) && isNumeric(b):
		return kvspace.NewBool(o.f(asFloat(a), asFloat(b))), nil
	case a.Kind() == "string" && b.Kind() == "string":
		return kvspace.NewBool(o.s(a.String(), b.String())), nil
	case a.Kind() == "bool" && b.Kind() == "bool":
		return kvspace.NewBool(o.s(strconv.FormatBool(AsBool(a)), strconv.FormatBool(AsBool(b)))), nil
	default:
		return kvspace.None{}, fmt.Errorf("TypeError: cannot compare %s with %s", a.Kind(), b.Kind())
	}
}
