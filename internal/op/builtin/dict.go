package builtin

import (
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

// dictOp: dict() → empty dict (just returns marker)
type dictOp struct{}
func (dictOp) Call(f *op.Frame) error {
	return writeResult(f, kvspace.Str("__dict__"))
}

// dictSetOp: dset(dict, key, val) → dict (set key→val in kvspace)
type dictSetOp struct{}
func (dictSetOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 3 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "dset requires dict, key, val")
		return nil
	}
	key := inputs[1].Str()
	// write to frame local path: dict/key = val
	outKey := resolveWriteKey(keytree.FrameRoot(f.PC), f.Inst.Writes[0])
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return f.KV.Set(keytree.Member(outKey, key), inputs[2])
}

// dictGetOp: dget(dict, key) → val or nil
type dictGetOp struct{}
func (dictGetOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 { return writeResult(f, kvspace.XValue{}) }
	key := inputs[1].Str()
	framePath := keytree.FrameRoot(f.PC)
	dictPath := resolveWriteKey(framePath, f.Inst.Reads[0])
	v, _ := f.KV.Get(keytree.Member(dictPath, key))
	return writeResult(f, v)
}
