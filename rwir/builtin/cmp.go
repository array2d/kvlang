package builtin

import (
	"fmt"
	"strconv"

	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	Register("==", "rwir ==(A:num, B:num) -> (C:bool)", cmp{f: func(a, b float64) bool { return a == b }, i: func(a, b int64) bool { return a == b }, s: func(a, b string) bool { return a == b }, allowNull: true})
	Register("!=", "rwir !=(A:num, B:num) -> (C:bool)", cmp{f: func(a, b float64) bool { return a != b }, i: func(a, b int64) bool { return a != b }, s: func(a, b string) bool { return a != b }, allowNull: true})
	Register("<",  "rwir <(A:num, B:num) -> (C:bool)",  cmp{f: func(a, b float64) bool { return a < b },  i: func(a, b int64) bool { return a < b },  s: func(a, b string) bool { return a < b }})
	Register(">",  "rwir >(A:num, B:num) -> (C:bool)",  cmp{f: func(a, b float64) bool { return a > b },  i: func(a, b int64) bool { return a > b },  s: func(a, b string) bool { return a > b }})
	Register("<=", "rwir <=(A:num, B:num) -> (C:bool)", cmp{f: func(a, b float64) bool { return a <= b }, i: func(a, b int64) bool { return a <= b }, s: func(a, b string) bool { return a <= b }})
	Register(">=", "rwir >=(A:num, B:num) -> (C:bool)", cmp{f: func(a, b float64) bool { return a >= b }, i: func(a, b int64) bool { return a >= b }, s: func(a, b string) bool { return a >= b }})
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
	if err := requireBinary(inputs); err != nil { return kvspace.XValue{}, err }
	a, b := inputs[0], inputs[1]

	// null kind 比较：仅 ==/!= 允许
	if a.IsNone() || b.IsNone() {
		if !o.allowNull {
			return kvspace.XValue{}, fmt.Errorf("TypeError: None in comparison")
		}
		return kvspace.Bool(o.s(a.Kind(), b.Kind())), nil
	}

	// 同类型直接比较；混合类型 → TypeError（p0/p1：不容忍跨类型静默比较）
	switch {
	case isIntKind(a.Kind()) && isIntKind(b.Kind()) && o.i != nil:
		return kvspace.Bool(o.i(asInt(a), asInt(b))), nil
	case isNumeric(a) && isNumeric(b):
		return kvspace.Bool(o.f(asFloat(a), asFloat(b))), nil
	case a.Kind() == "string" && b.Kind() == "string":
		return kvspace.Bool(o.s(a.Str(), b.Str())), nil
	case a.Kind() == "bool" && b.Kind() == "bool":
		return kvspace.Bool(o.s(strconv.FormatBool(a.Bool()), strconv.FormatBool(b.Bool()))), nil
	default:
		return kvspace.XValue{}, fmt.Errorf("TypeError: cannot compare %s with %s", a.Kind(), b.Kind())
	}
}
