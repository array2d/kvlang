package builtin

import (
	"fmt"

	"oldhero/keytree"
	"github.com/array2d/kvspace-go"
	"oldhero/logx"
	"oldhero/rwir"
	"oldhero/vthread"
)

func init() {
	Register("string.set",    "rwir string.set(A:any) -> (C:char/utf32)", strOp{})
	Register("string.char",   "rwir string.char(S:char/utf32, I:int64) -> (C:char/utf32)", strCharOp{})
	Register("string.ord",    "rwir string.ord(S:char/utf32) -> (C:int64)", strOrdOp{})
	Register("string.cmp",    "rwir string.cmp(A:char/utf32, B:char/utf32) -> (C:int64)", strCmpOp{})
	Register("string.find",   "rwir string.find(Hay:char/utf32, Needle:char/utf32) -> (C:int64)", strStrOp{})
	Register("string.len",    "rwir string.len(S:char/utf32) -> (C:int64)", strLenOp{})
	Register("string.slice",  "rwir string.slice(S:char/utf32, Lo:int64, Hi:int64) -> (C:char/utf32)", strSliceOp{})
	Register("string.concat", "rwir string.concat(A:char/utf32, B:char/utf32) -> (C:char/utf32)", strConcatOp{})
}

// stringRunes 返回字符串的码点切片。char/utf32 直接，char/utf8 解码。
func stringRunes(v kvspace.XValue) []rune {
	if c, ok := v.(kvspace.Char32); ok {
		r := make([]rune, int(c.ArrayLen()))
		for i := range r {
			r[i] = c.At(i)
		}
		return r
	}
	return []rune(v.ValueString())
}

// varLenCharErr 返回变长字符编码（char/utf8）的索引拒绝错误；定宽编码返回 nil。
func varLenCharErr(kind string) error {
	if kind == kvspace.KindCharUtf8 {
		return fmt.Errorf("TypeError: char/utf8 is variable-width; index/code-point ops require char/utf32 or char/ascii")
	}
	return nil
}

// ── string.set ────────────────────────────────────────────────────

type strOp struct{}
func (strOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	val := ""
	if len(inputs) > 0 { val = display(inputs[0]) }
	if len(f.Inst.Writes) > 0 {
		wKey := writeSlotKey(f.KV, keytree.FrameRoot(f.PC), f.Inst.Writes[0].Name)
		f.KV.Set([]kvspace.KVPair{{Key: wKey, Val: kvspace.NewChar(kvspace.KindChar, val)}})
	}
	logx.Debug("[%s] string.set %q -> %s", f.Vtid, val, f.Inst.Writes)
	nextPC(f)
	return nil
}

// ── string.char ───────────────────────────────────────────────────

type strCharOp struct{}
func (strCharOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: string.char requires string and index")
		return fmt.Errorf("TypeError: string.char requires string and index")
	}
	if err := varLenCharErr(inputs[0].Kind()); err != nil {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error())
		return err
	}
	idx := int(asInt64(inputs[1]))
	if c, ok := inputs[0].(kvspace.Char32); ok {
		if idx < 0 || idx >= int(c.ArrayLen()) {
			vthread.SetError(bg, f.KV, f.Vtid, f.PC,
				fmt.Sprintf("IndexError: at: index %d out of bounds (char count=%d)", idx, c.ArrayLen()))
			return fmt.Errorf("IndexError: char index out of bounds")
		}
		return writeResult(f, kvspace.NewChar32(c.At(idx)))
	}
	runes := stringRunes(inputs[0])
	if idx < 0 || idx >= len(runes) {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: at: index %d out of bounds (char count=%d)", idx, len(runes)))
		return fmt.Errorf("IndexError: char index out of bounds")
	}
	return writeResult(f, kvspace.NewChar32(runes[idx]))
}

// ── string.ord ────────────────────────────────────────────────────

type strOrdOp struct{}
func (strOrdOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: string.ord requires a string")
		return fmt.Errorf("TypeError: string.ord requires a string")
	}
	if err := varLenCharErr(inputs[0].Kind()); err != nil {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error())
		return err
	}
	if c, ok := inputs[0].(kvspace.Char32); ok {
		if c.ArrayLen() == 0 { return writeResult(f, kvspace.NewInt64(-1)) }
		return writeResult(f, kvspace.NewInt64(int64(c.At(0))))
	}
	runes := stringRunes(inputs[0])
	if len(runes) == 0 { return writeResult(f, kvspace.NewInt64(-1)) }
	return writeResult(f, kvspace.NewInt64(int64(runes[0])))
}

// ── string.cmp ────────────────────────────────────────────────────

type strCmpOp struct{}
func (strCmpOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: string.cmp requires two strings")
		return fmt.Errorf("TypeError: string.cmp requires two strings")
	}
	a, b := inputs[0].ValueString(), inputs[1].ValueString()
	r := int64(0)
	if a < b { r = -1 } else if a > b { r = 1 }
	return writeResult(f, kvspace.NewInt64(r))
}

// ── string.find ───────────────────────────────────────────────────

type strStrOp struct{}
func (strStrOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: string.find requires two strings")
		return fmt.Errorf("TypeError: string.find requires two strings")
	}
	if err := varLenCharErr(inputs[0].Kind()); err != nil {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error())
		return err
	}
	hay := stringRunes(inputs[0])
	needle := stringRunes(inputs[1])
	if len(needle) == 0 { return writeResult(f, kvspace.NewInt64(0)) }
	for i := 0; i+len(needle) <= len(hay); i++ {
		match := true
		for j := range needle {
			if hay[i+j] != needle[j] { match = false; break }
		}
		if match { return writeResult(f, kvspace.NewInt64(int64(i))) }
	}
	return writeResult(f, kvspace.NewInt64(-1))
}

// ── string.len ────────────────────────────────────────────────────

type strLenOp struct{}
func (strLenOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	n := 0
	if len(inputs) > 0 {
		if err := varLenCharErr(inputs[0].Kind()); err != nil {
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error())
			return err
		}
		if c, ok := inputs[0].(kvspace.Char32); ok {
			n = int(c.ArrayLen())
		} else {
			n = len(stringRunes(inputs[0]))
		}
	}
	return writeResult(f, kvspace.NewInt64(int64(n)))
}

// ── string.slice ──────────────────────────────────────────────────

type strSliceOp struct{}
func (strSliceOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 3 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: string.slice requires string, start, end")
		return fmt.Errorf("TypeError: string.slice requires string, start, end")
	}
	if err := varLenCharErr(inputs[0].Kind()); err != nil {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error())
		return err
	}
	lo, hi := int(asInt64(inputs[1])), int(asInt64(inputs[2]))
	runes := stringRunes(inputs[0])
	n := len(runes)
	if lo < 0 || hi > n || lo > hi {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: at: slice index out of bounds (lo=%d hi=%d char count=%d)", lo, hi, n))
		return fmt.Errorf("IndexError: slice index out of bounds")
	}
	if lo >= hi { return writeResult(f, kvspace.NewChar32()) }
	return writeResult(f, kvspace.NewChar32(runes[lo:hi]...))
}

// ── string.concat ─────────────────────────────────────────────────

type strConcatOp struct{}
func (strConcatOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 { return writeResult(f, kvspace.NewChar32()) }
	if !kvspace.IsCharKind(inputs[0].Kind()) || !kvspace.IsCharKind(inputs[1].Kind()) {
		msg := fmt.Sprintf("TypeError: string.concat requires strings, got %s and %s", inputs[0].Kind(), inputs[1].Kind())
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	runes := append(stringRunes(inputs[0]), stringRunes(inputs[1])...)
	return writeResult(f, kvspace.NewChar32(runes...))
}
