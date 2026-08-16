package builtin

import (
	"fmt"
	"strconv"

	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	eqCmp := cmp{f: func(a, b float64) bool { return a == b }, p: func(c int) bool { return c == 0 }, s: func(a, b string) bool { return a == b }, allowNull: true}
	registerKinds("eq", 2, "bool", numKinds, eqCmp)
	neqCmp := cmp{f: func(a, b float64) bool { return a != b }, p: func(c int) bool { return c != 0 }, s: func(a, b string) bool { return a != b }, allowNull: true}
	registerKinds("neq", 2, "bool", numKinds, neqCmp)
	registerKinds("lt", 2, "bool", numKinds, cmp{f: func(a, b float64) bool { return a < b }, p: func(c int) bool { return c < 0 }, s: func(a, b string) bool { return a < b }})
	registerKinds("gt", 2, "bool", numKinds, cmp{f: func(a, b float64) bool { return a > b }, p: func(c int) bool { return c > 0 }, s: func(a, b string) bool { return a > b }})
	registerKinds("le", 2, "bool", numKinds, cmp{f: func(a, b float64) bool { return a <= b }, p: func(c int) bool { return c <= 0 }, s: func(a, b string) bool { return a <= b }})
	registerKinds("ge", 2, "bool", numKinds, cmp{f: func(a, b float64) bool { return a >= b }, p: func(c int) bool { return c >= 0 }, s: func(a, b string) bool { return a >= b }})
}

type cmp struct {
	f         func(float64, float64) bool
	p         func(int) bool // int 比较谓词（cmpInt 返回 -1/0/1）
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
	case isIntKind(a.Kind()) && isIntKind(b.Kind()) && o.p != nil:
		return kvspace.NewBool(o.p(cmpInt(a, b))), nil
	case isNumeric(a) && isNumeric(b):
		return kvspace.NewBool(o.f(asFloat(a), asFloat(b))), nil
	case kvspace.IsCharKind(a.Kind()) && kvspace.IsCharKind(b.Kind()):
		return kvspace.NewBool(o.s(a.ValueString(), b.ValueString())), nil
	case a.Kind() == "bool" && b.Kind() == "bool":
		return kvspace.NewBool(o.s(strconv.FormatBool(AsBool(a)), strconv.FormatBool(AsBool(b)))), nil
	default:
		return kvspace.None{}, fmt.Errorf("TypeError: cannot compare %s with %s", a.Kind(), b.Kind())
	}
}

// cmpInt 返回 a 与 b 的数学大小关系（-1/0/1），正确处理 signed/unsigned 混合。
// uint64 > 2^63 不因 asInt64 回绕而判错。
func cmpInt(a, b kvspace.XValue) int {
	aUnsigned := isUnsignedKind(a.Kind())
	bUnsigned := isUnsignedKind(b.Kind())
	switch {
	case !aUnsigned && !bUnsigned:
		ai, bi := asInt64(a), asInt64(b)
		if ai < bi {
			return -1
		}
		if ai > bi {
			return 1
		}
		return 0
	case aUnsigned && bUnsigned:
		au, bu := asUint64(a), asUint64(b)
		if au < bu {
			return -1
		}
		if au > bu {
			return 1
		}
		return 0
	case aUnsigned && !bUnsigned:
		bi := asInt64(b)
		if bi < 0 {
			return 1 // a >= 0 > b
		}
		au := asUint64(a)
		if au < uint64(bi) {
			return -1
		}
		if au > uint64(bi) {
			return 1
		}
		return 0
	default:
		ai := asInt64(a)
		if ai < 0 {
			return -1 // a < 0 <= b
		}
		bu := asUint64(b)
		if uint64(ai) < bu {
			return -1
		}
		if uint64(ai) > bu {
			return 1
		}
		return 0
	}
}
