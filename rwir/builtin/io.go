package builtin

import (
	"fmt"
	"strings"

	"kvlang/device"
	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/logx"
	"kvlang/rwir"
)

func init() {
	Register("print", "rwir print(A:any, ...) -> ()",       ioOp{print: true, nosep: true, rawnl: true})
	Register("println", "rwir println(A:any, ...) -> ()",       ioOp{print: true})
	Register("cerr",  "rwir cerr(A:any, ...) -> ()",        ioOp{print: true, cerr: true})
	Register("input", "rwir input(prompt:string?) -> (C:string)", ioOp{input: true})
}

type ioOp struct{ print, cerr, input, nosep, rawnl bool }
func (o ioOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if o.print {
		stream := "stdout"
		if o.cerr { stream = "stderr" }
		parts := make([]string, len(inputs))
		for i, v := range inputs { parts[i] = display(v) }
		sep := " "; if o.nosep { sep = "" }; line := strings.Join(parts, sep)
		ts := device.ResolveTerm(bg, f.KV, f.Vtid, stream)
		if !ts.IsZero() { if o.rawnl { device.WriteTermRaw(bg, ts, line) } else { device.WriteTerm(bg, ts, line) } }
		logx.Debug("PRINT %s", line)
		nextPC(f)
		return nil
	}
	if o.input {
		if len(inputs) > 0 {
			ts := device.ResolveTerm(bg, f.KV, f.Vtid, "stdout")
			if !ts.IsZero() { device.WriteTerm(bg, ts, display(inputs[0])) }
		}
		ts := device.ResolveTerm(bg, f.KV, f.Vtid, "stdin")
		var val string
		if !ts.IsZero() { val, _ = device.ReadTerm(bg, ts) }
		if len(f.Inst.Writes) > 0 {
			wKey := writeSlotKey(f.KV, keytree.FrameRoot(f.PC), f.Inst.Writes[0].Name)
			f.KV.Set([]kvspace.KVPair{{Key: wKey, Val: kvspace.NewStringByte([]byte(val)...)}})
		}
		nextPC(f)
		return nil
	}
	return fmt.Errorf("ioOp: no mode")
}
