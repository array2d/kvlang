package builtin

import (
	"strings"
	"fmt"
	"github.com/array2d/kvspace-go"
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
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: char requires string and index")
		return fmt.Errorf("TypeError: char requires string and index")
	}
	s := inputs[0].Str()
	idx := int(inputs[1].Int64())
	if idx < 0 || idx >= len(s) {
		return writeResult(f, kvspace.Str(""))
	}
	return writeResult(f, kvspace.Str(s[idx:idx+1]))
}

// strOrdOp: ord(c) -> int —— 返回单字符字符串的字节码（fix-024 配套；Python 阵营，见 p7）。
// 按索引取码用组合：ord(char(s, i))。空串返回 -1（缺席语义）。
type strOrdOp struct{}
func (strOrdOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: ord requires a string")
		return fmt.Errorf("TypeError: ord requires a string")
	}
	s := inputs[0].Str()
	if len(s) == 0 {
		return writeResult(f, kvspace.Int64(-1))
	}
	return writeResult(f, kvspace.Int64(int64(s[0])))
}

// strCmpOp: strcmp(a, b) -> int —— C 语义：a<b 返 -1，相等返 0，a>b 返 1（按字节序）。
type strCmpOp struct{}
func (strCmpOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: strcmp requires two strings")
		return fmt.Errorf("TypeError: strcmp requires two strings")
	}
	a, b := inputs[0].Str(), inputs[1].Str()
	r := int64(0)
	if a < b { r = -1 } else if a > b { r = 1 }
	return writeResult(f, kvspace.Int64(r))
}

// strStrOp: strstr(hay, needle) -> int —— C 名 + 索引语义（C 返指针无法值语义化，
// 返首次出现的下标，未找到返 -1，同 Python find；fix-025 记录为融合形态）。
type strStrOp struct{}
func (strStrOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: strstr requires two strings")
		return fmt.Errorf("TypeError: strstr requires two strings")
	}
	idx := strings.Index(inputs[0].Str(), inputs[1].Str())
	return writeResult(f, kvspace.Int64(int64(idx)))
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
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: slice requires string, start, end")
		return fmt.Errorf("TypeError: slice requires string, start, end")
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
