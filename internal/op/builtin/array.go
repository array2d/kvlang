package builtin

import (
	"encoding/binary"
	"fmt"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

// arrayOp: array(elem1, elem2, ...) → 1D array XValue
type arrayOp struct{}
func (arrayOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	arr := kvspace.Array(inputs)
	if len(f.Inst.Writes) > 0 {
		outKey := resolveWriteKey(keytree.FrameRoot(f.PC), f.Inst.Writes[0])
		if err := f.KV.Set(outKey, arr); err != nil { return err }
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

// lenOp: len(array) → int
type lenOp struct{}
func (lenOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	n := 0
	if len(inputs) > 0 {
		n = inputs[0].Len()
	}
	return writeResult(f, kvspace.Int64(int64(n)))
}

// atOp: at(array, index) → element
type atOp struct{}
func (atOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "at requires array and index")
		return fmt.Errorf("at requires array and index")
	}
	idx := int(inputs[1].Int64())
	elem := inputs[0].Index(idx)
	if elem.IsNil() {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("at: index %d out of bounds", idx))
		return fmt.Errorf("at: index out of bounds")
	}
	return writeResult(f, elem)
}

// arraySetOp: set(array, index, value) → modified array
type arraySetOp struct{}
func (arraySetOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 3 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "set requires array, index, value")
		return fmt.Errorf("set requires array, index, value")
	}
	arr := inputs[0]
	idx := int(inputs[1].Int64())
	if idx < 0 || idx >= arr.Len() {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("set: index %d out of bounds (len=%d)", idx, arr.Len()))
		return fmt.Errorf("set: index out of bounds")
	}
	val := inputs[2]
	// Rebuild array with modified element
	n := arr.Len()
	total := 4
	encoded := make([][]byte, n)
	for i := 0; i < n; i++ {
		elem := arr.Index(i)
		if i == idx { elem = val }
		encoded[i] = kvspace.EncodeXValue(elem)
		total += len(encoded[i])
	}
	raw := make([]byte, total)
	binary.LittleEndian.PutUint32(raw[:4], uint32(n))
	off := 4
	for _, enc := range encoded {
		copy(raw[off:], enc)
		off += len(enc)
	}
	if len(f.Inst.Writes) > 0 {
		outKey := resolveWriteKey(keytree.FrameRoot(f.PC), f.Inst.Writes[0])
		if err := f.KV.Set(outKey, kvspace.Raw("array", raw)); err != nil { return err }
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}
