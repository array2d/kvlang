package builtin

import (
	"fmt"

	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	registerWord("bitand", "rwir bitand(A:int64, B:int64) -> (C:int64)", bit{f: func(a, b int64) int64 { return a & b }})
	registerWord("bitor",  "rwir bitor(A:int64, B:int64) -> (C:int64)",  bit{f: func(a, b int64) int64 { return a | b }})
	registerWord("bitxor", "rwir bitxor(A:int64, B:int64) -> (C:int64)", bit{f: func(a, b int64) int64 { return a ^ b }})
	registerWord("shl",    "rwir shl(A:int64, B:int64) -> (C:int64)",    bit{f: func(a, b int64) int64 { return a << uint64(b) }})
	registerWord("shr",    "rwir shr(A:int64, B:int64) -> (C:int64)",    bit{f: func(a, b int64) int64 { return a >> uint64(b) }})
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
	return kvspace.NewInt64(fn(asInt(inputs[0]), asInt(inputs[1]))), nil
}
