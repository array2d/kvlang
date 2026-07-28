package builtin

import (
	"strings"
	"fmt"
	"github.com/array2d/kvspace-go"
	"kvlang/op"
	"kvlang/vthread"
)

// strCharOp: char(s, i) -> str —— 返回 s 的第 i 个 Unicode 字符（单字符字符串）。
// fix-024：原返回字节码 int，与 Python s[i]/JS charAt 的动态语言直觉相悖
// （kvlang 无独立 char 类型，字符即单字符 string）。
// RC4：改用 []rune 使索引以 Unicode 字符为单位；越界报 IndexError。
type strCharOp struct{}
func (strCharOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: char requires string and index")
		return fmt.Errorf("TypeError: char requires string and index")
	}
	s := inputs[0].Str()
	idx := int(inputs[1].Int64())
	runes := []rune(s)
	if idx < 0 || idx >= len(runes) {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: at: index %d out of bounds (char count=%d)", idx, len(runes)))
		return fmt.Errorf("IndexError: char index out of bounds")
	}
	return writeResult(f, kvspace.Str(string(runes[idx])))
}

// strOrdOp: ord(c) -> int —— 返回单字符字符串的 Unicode 码点值（fix-024 配套；Python 阵营，见 p7）。
// 按索引取码用组合：ord(char(s, i))。空串返回 -1（缺席语义）。
// RC4：改用 []rune 取 Unicode 码点而非首字节值。
type strOrdOp struct{}
func (strOrdOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: ord requires a string")
		return fmt.Errorf("TypeError: ord requires a string")
	}
	s := inputs[0].Str()
	runes := []rune(s)
	if len(runes) == 0 {
		return writeResult(f, kvspace.Int64(-1))
	}
	return writeResult(f, kvspace.Int64(int64(runes[0])))
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

// strLenOp: strlen(s) -> int —— 返回字符串的 Unicode 字符数（RC4：由字节数改为字符数）。
type strLenOp struct{}
func (strLenOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	n := 0
	if len(inputs) > 0 { n = len([]rune(inputs[0].Str())) }
	return writeResult(f, kvspace.Int64(int64(n)))
}

// strSliceOp: slice(s, i, j) -> string (substring s[i:j] by Unicode characters)
// RC4：改用 []rune 使切片以 Unicode 字符为单位；越界报 IndexError。
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
	runes := []rune(s)
	runeLen := len(runes)
	if lo < 0 || hi > runeLen || lo > hi {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: at: slice index out of bounds (lo=%d hi=%d char count=%d)", lo, hi, runeLen))
		return fmt.Errorf("IndexError: slice index out of bounds")
	}
	if lo >= hi {
		return writeResult(f, kvspace.Str(""))
	}
	return writeResult(f, kvspace.Str(string(runes[lo:hi])))
}

// strConcatOp: concat(a, b) -> string
type strConcatOp struct{}
func (strConcatOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 { return writeResult(f, kvspace.Str("")) }
	if inputs[0].Kind() != "string" || inputs[1].Kind() != "string" {
		msg := fmt.Sprintf("TypeError: concat requires strings, got %s and %s", inputs[0].Kind(), inputs[1].Kind())
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	return writeResult(f, kvspace.Str(inputs[0].Str()+inputs[1].Str()))
}
