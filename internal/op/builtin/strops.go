package builtin

import (
	"fmt"
	"github.com/array2d/kvlang-go"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

// strCharOp: char(s, i) -> str —— 返回 s 的第 i 个字符（单字符字符串）。
// fix-024：原返回字节码 int，与 Python s[i]/JS charAt 的动态语言直觉相悖
// （kvlang 无独立 char 类型，字符即单字符 string）；越界返回 ""（缺席语义）。
type strCharOp struct{}
func (strCharOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "char requires string and index")
		return fmt.Errorf("char requires string and index")
	}
	s := inputs[0].Str()
	idx := int(inputs[1].Int64())
	if idx < 0 || idx >= len(s) {
		return writeResult(f, kvspace.Str(""))
	}
	return writeResult(f, kvspace.Str(s[idx:idx+1]))
}

// strOrdOp: ord(s) / ord(s, i) -> int —— 返回 s 第 i 个字符（默认 0）的字节码（fix-024 配套）。
type strOrdOp struct{}
func (strOrdOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "ord requires a string")
		return fmt.Errorf("ord requires a string")
	}
	s := inputs[0].Str()
	idx := 0
	if len(inputs) >= 2 { idx = int(inputs[1].Int64()) }
	if idx < 0 || idx >= len(s) {
		return writeResult(f, kvspace.Int64(-1))
	}
	return writeResult(f, kvspace.Int64(int64(s[idx])))
}

// strLenOp: strlen(s) -> int
type strLenOp struct{}
func (strLenOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	n := 0
	if len(inputs) > 0 { n = len(inputs[0].Str()) }
	return writeResult(f, kvspace.Int64(int64(n)))
}

// strSliceOp: slice(s, i, j) -> string (substring s[i:j])
type strSliceOp struct{}
func (strSliceOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 3 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "slice requires string, start, end")
		return fmt.Errorf("slice requires string, start, end")
	}
	s := inputs[0].Str()
	lo := int(inputs[1].Int64())
	hi := int(inputs[2].Int64())
	if lo < 0 { lo = 0 }
	if hi > len(s) { hi = len(s) }
	if lo >= hi { return writeResult(f, kvspace.Str("")) }
	return writeResult(f, kvspace.Str(s[lo:hi]))
}

// strConcatOp: concat(a, b) -> string
type strConcatOp struct{}
func (strConcatOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 { return writeResult(f, kvspace.Str("")) }
	return writeResult(f, kvspace.Str(inputs[0].Str()+inputs[1].Str()))
}
