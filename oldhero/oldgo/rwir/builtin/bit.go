package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"oldhero/rwir"
	"oldhero/vthread"
)

func init() {
	registerKinds("bitand", 2, "", []string{"int64", "uint64"}, bit{f: func(a, b int64) int64 { return a & b }})
	registerKinds("bitor", 2, "", []string{"int64", "uint64"}, bit{f: func(a, b int64) int64 { return a | b }})
	registerKinds("bitxor", 2, "", []string{"int64", "uint64"}, bit{f: func(a, b int64) int64 { return a ^ b }})
	registerKinds("shl", 2, "", []string{"int64", "uint64"}, bit{f: func(a, b int64) int64 { return a << uint64(b) }})
	registerKinds("shr", 2, "", []string{"int64", "uint64"}, bit{f: func(a, b int64) int64 { return a >> uint64(b) }})
}

type bit struct{ f func(int64, int64) int64 }
func (o bit) Call(f *rwir.Frame) error {
	r, err := evalBinaryInt(readInputs(f), o.f)
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalBinaryInt(inputs []kvspace.XValue, fn func(int64, int64) int64) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.None{}, err }
	if kvspace.IsNone(inputs[0]) || kvspace.IsNone(inputs[1]) {
		return kvspace.None{}, fmt.Errorf("TypeError: None in bitwise operation")
	}
	// 位运算仅整数（五语言一致：C/Rust/Go/JS 均禁止浮点位运算，Python & 是 set intersection）
	if err := requireInt(inputs[0], inputs[1]); err != nil { return kvspace.None{}, err }
	return narrowInt(inputs[0].Kind(), inputs[1].Kind(), fn(asInt64(inputs[0]), asInt64(inputs[1]))), nil
}
