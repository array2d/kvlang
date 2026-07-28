package builtin

import (
	"fmt"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/op"
	"kvlang/vthread"
)

func requireBinary(inputs []kvspace.XValue) error {
	if len(inputs) != 2 { return fmt.Errorf("binary op requires 2 inputs, got %d", len(inputs)) }
	return nil
}
func requireUnary(inputs []kvspace.XValue) error {
	if len(inputs) != 1 { return fmt.Errorf("unary op requires 1 input, got %d", len(inputs)) }
	return nil
}

// requireNumeric guards that all inputs are numeric (int or float kinds).
func requireNumeric(inputs ...kvspace.XValue) error {
	for _, v := range inputs {
		if !isNumeric(v) {
			return fmt.Errorf("TypeError: expected numeric, got %s", v.Kind())
		}
	}
	return nil
}

// requireInt guards that all inputs are integer kinds.
func requireInt(inputs ...kvspace.XValue) error {
	for _, v := range inputs {
		if !isIntKind(v.Kind()) {
			return fmt.Errorf("TypeError: expected integer, got %s", v.Kind())
		}
	}
	return nil
}

// requireKind guards that an input has a specific kind.
func requireKind(v kvspace.XValue, kind string) error {
	if v.Kind() != kind {
		return fmt.Errorf("TypeError: expected %s, got %s", kind, v.Kind())
	}
	return nil
}

// readInputs resolves all read-slots of f.Inst into typed Values.
func readInputs(f *op.Frame) []kvspace.XValue {
	framePath := keytree.FrameRoot(f.PC)
	inputs := make([]kvspace.XValue, 0, len(f.Inst.Reads))
	for _, r := range f.Inst.Reads {
		inputs = append(inputs, resolveReadValue(f.KV, framePath, r))
	}
	return inputs
}

// writeResult writes a typed Value to the first write-slot and advances PC.
func setWrite(kv kvspace.KVSpace, framePath, slot string, val kvspace.XValue) error {
	return kv.Set([]kvspace.KVPair{{resolveWriteKey(kv, framePath, slot), val}})
}

func writeResult(f *op.Frame, result kvspace.XValue) error {
	if len(f.Inst.Writes) > 0 {
		if err := setWrite(f.KV, keytree.FrameRoot(f.PC), f.Inst.Writes[0], result); err != nil {
			return err
		}
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

// nextPC advances PC without writing a result.
func nextPC(f *op.Frame) {
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
}

// funcFrameRoot returns the nearest rwfunc frame root from the given frame path.
// Relative path resolution (kvhas/kvat/at/set) must use the function frame root,
// not the current label frame root, because data lives under the function frame.
func funcFrameRoot(kv kvspace.KVSpace, frameRoot string) string {
	for f := frameRoot; f != ""; f = keytree.ParentFrame(f) {
		if extKind(kv, f) == kvspace.KindRwfunc {
			return f
		}
	}
	return frameRoot
}

// ExecuteCopy copies the Value addressed by inst.Reads[0] to all write-slots.
// Preserves the original type — int stays int, float stays float.
func ExecuteCopy(kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	framePath := keytree.FrameRoot(pc)
	var src string
	if len(inst.Reads) > 0 { src = inst.Reads[0] }
	v := resolveReadValue(kv, framePath, src)
	for _, w := range inst.Writes {
		if err := setWrite(kv, framePath, w, v); err != nil {
			return err
		}
	}
	vthread.Set(bg, kv, vtid, op.NextPC(pc), "running")
	return nil
}
