package builtin

import (
	"kvlang/internal/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/internal/logx"
	"kvlang/internal/op"
)

type strOp struct{}
func (strOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	val := ""
	if len(inputs) > 0 { val = display(inputs[0]) }
	if len(f.Inst.Writes) > 0 {
		wKey := resolveWriteKey(f.KV, keytree.FrameRoot(f.PC), f.Inst.Writes[0])
		f.KV.Set([]kvspace.KVPair{{wKey, kvspace.Str(val)}})
	}
	logx.Debug("[%s] string.set %q -> %s", f.Vtid, val, f.Inst.Writes)
	nextPC(f)
	return nil
}
