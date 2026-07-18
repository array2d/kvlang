package builtin

import (
	"fmt"
	"strings"

	"kvlang/internal/device"
	"kvlang/internal/keytree"
	"github.com/array2d/kvlang-go"
	"kvlang/internal/logx"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type ioOp struct{ print, cerr, input bool }
func (o ioOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if o.print {
		stream := "stdout"
		if o.cerr { stream = "stderr" }
		parts := make([]string, len(inputs))
		for i, v := range inputs { parts[i] = display(v) }
		line := strings.Join(parts, " ")
		ts := device.ResolveTerm(bg, f.KV, f.Vtid, stream)
		if !ts.IsZero() { device.WriteTerm(bg, ts, line) }
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
			wKey := resolveWriteKey(keytree.FrameRoot(f.PC), f.Inst.Writes[0])
			if err := f.KV.Set(wKey, kvspace.Str(val)); err != nil {
				vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error())
				return err
			}
		}
		nextPC(f)
		return nil
	}
	return fmt.Errorf("ioOp: no mode")
}
