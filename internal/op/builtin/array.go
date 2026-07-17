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
	// 字符串字段名：at(base, "field") → kv.Get(base/field)
	if inputs[1].Kind() == "string" {
		fp := keytree.FrameRoot(f.PC)
		baseVal := resolveReadValue(f.KV, fp, f.Inst.Reads[0])
		path := baseVal.Str() + "/" + inputs[1].Str()
		v, _ := f.KV.Get(path)
		if v.IsNil() {
			vthread.SetError(bg, f.KV, f.Vtid, f.PC,
				fmt.Sprintf("at: key not found: %s", path))
			return fmt.Errorf("at: key not found: %s", path)
		}
		return writeResult(f, v)
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
	// 字段写入：set(base, "field", val) → kv.Set(base/field, val)
	if inputs[1].Kind() == "string" {
		fp := keytree.FrameRoot(f.PC)
		baseVal := resolveReadValue(f.KV, fp, f.Inst.Reads[0])
		path := baseVal.Str() + "/" + inputs[1].Str()
		f.KV.Set(path, inputs[2])
		if len(f.Inst.Writes) > 0 {
			// 写入 base 本身（不变），满足 -> base 返回槽
			outKey := resolveWriteKey(fp, f.Inst.Writes[0])
			f.KV.Set(outKey, baseVal)
		}
		vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
		return nil
	}
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

// sortOp: bubble sort (in-place, returns sorted copy)
type sortOp struct{}
func (sortOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 { return writeResult(f, kvspace.XValue{}) }
	arr := inputs[0]
	n := arr.Len()
	elems := make([]kvspace.XValue, n)
	for i := 0; i < n; i++ { elems[i] = arr.Index(i) }
	// bubble sort
	for i := 0; i < n-1; i++ {
		for j := 0; j < n-i-1; j++ {
			if asFloat(elems[j]) > asFloat(elems[j+1]) {
				elems[j], elems[j+1] = elems[j+1], elems[j]
			}
		}
	}
	result := kvspace.Array(elems)
	return writeResult(f, result)
}
