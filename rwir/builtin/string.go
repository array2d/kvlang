package builtin

import (
	"fmt"
	"strings"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/logx"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	Register("string.set",    "rwir string.set(A:any) -> (...)", strOp{})
	Register("string.char",   "rwir string.char(S:string, I:int64) -> (C:string)", strCharOp{})
	Register("string.ord",    "rwir string.ord(S:string) -> (C:int64)",          strOrdOp{})
	Register("string.cmp",    "rwir string.cmp(A:string, B:string) -> (C:int64)", strCmpOp{})
	Register("string.find",   "rwir string.find(Hay:string, Needle:string) -> (C:int64)", strStrOp{})
	Register("string.len",    "rwir string.len(S:string) -> (C:int64)", strLenOp{})
	Register("string.slice",  "rwir string.slice(S:string, Lo:int64, Hi:int64) -> (C:string)", strSliceOp{})
	Register("string.concat", "rwir string.concat(A:string, B:string) -> (C:string)", strConcatOp{})
}

// ── string.set ────────────────────────────────────────────────────

type strOp struct{}
func (strOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	val := ""
	if len(inputs) > 0 { val = display(inputs[0]) }
	if len(f.Inst.Writes) > 0 {
		wKey := writeSlotKey(f.KV, keytree.FrameRoot(f.PC), f.Inst.Writes[0].Name)
		f.KV.Set([]kvspace.KVPair{{Key: wKey, Val: kvspace.NewStringByte([]byte(val)...)}})
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
	s := inputs[0].ValueString()
	idx := int(asInt64(inputs[1]))
	runes := []rune(s)
	if idx < 0 || idx >= len(runes) {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: at: index %d out of bounds (char count=%d)", idx, len(runes)))
		return fmt.Errorf("IndexError: char index out of bounds")
	}
	return writeResult(f, kvspace.NewStringByte([]byte(string(runes[idx]))...))
}

// ── string.ord ────────────────────────────────────────────────────

type strOrdOp struct{}
func (strOrdOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: string.ord requires a string")
		return fmt.Errorf("TypeError: string.ord requires a string")
	}
	s := inputs[0].ValueString()
	runes := []rune(s)
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
	return writeResult(f, kvspace.NewInt64(int64(strings.Index(inputs[0].ValueString(), inputs[1].ValueString()))))
}

// ── string.len ────────────────────────────────────────────────────

type strLenOp struct{}
func (strLenOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	n := 0
	if len(inputs) > 0 { n = len([]rune(inputs[0].ValueString())) }
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
	lo, hi := int(asInt64(inputs[1])), int(asInt64(inputs[2]))
	runes := []rune(inputs[0].ValueString())
	n := len(runes)
	if lo < 0 || hi > n || lo > hi {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: at: slice index out of bounds (lo=%d hi=%d char count=%d)", lo, hi, n))
		return fmt.Errorf("IndexError: slice index out of bounds")
	}
	if lo >= hi { return writeResult(f, kvspace.NewStringByte([]byte("")...)) }
	return writeResult(f, kvspace.NewStringByte([]byte(string(runes[lo:hi]))...))
}

// ── string.concat ─────────────────────────────────────────────────

type strConcatOp struct{}
func (strConcatOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 { return writeResult(f, kvspace.NewStringByte([]byte("")...)) }
	if inputs[0].Kind() != "stringbyte" || inputs[1].Kind() != "stringbyte" {
		msg := fmt.Sprintf("TypeError: string.concat requires strings, got %s and %s", inputs[0].Kind(), inputs[1].Kind())
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	return writeResult(f, kvspace.NewStringByte([]byte(inputs[0].ValueString()+inputs[1].ValueString())...))
}
