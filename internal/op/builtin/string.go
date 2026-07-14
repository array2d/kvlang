package builtin

import (
	"kvlang/internal/keytree"
	"kvlang/internal/logx"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type strOp struct{}
func (strOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	val := ""
	if len(inputs) > 0 { val = inputs[0].String() }
	if len(f.Inst.Writes) > 0 {
		wKey := resolveWriteKey(keytree.FrameRoot(f.PC), f.Inst.Writes[0])
		if err := f.KV.Set(wKey, val); err != nil {
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error())
			return err
		}
	}
	logx.Debug("[%s] str.set %q -> %s", f.Vtid, val, f.Inst.Writes)
	nextPC(f)
	return nil
}
