package builtin

import (
	"kvlang/internal/keytree"
	"github.com/array2d/kvlang-go"
	"kvlang/internal/logx"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type strOp struct{}
func (strOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	val := ""
	if len(inputs) > 0 { val = display(inputs[0]) }
	if len(f.Inst.Writes) > 0 {
		wKey := resolveWriteKey(keytree.FrameRoot(f.PC), f.Inst.Writes[0])
		if err := f.KV.Set(wKey, kvspace.Str(val)); err != nil {
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error())
			return err
		}
	}
	logx.Debug("[%s] string.set %q -> %s", f.Vtid, val, f.Inst.Writes)
	nextPC(f)
	return nil
}
