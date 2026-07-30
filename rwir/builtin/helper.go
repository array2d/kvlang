package builtin

import (
	"fmt"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
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
func readInputs(f *rwir.Frame) []kvspace.XValue {
	framePath := funcFrameRoot(f.KV, keytree.FrameRoot(f.PC))
	inputs := make([]kvspace.XValue, 0, len(f.Inst.Reads))
	for _, r := range f.Inst.Reads {
		inputs = append(inputs, resolveReadValue(f.KV, framePath, r.Name))
	}
	return inputs
}

// writeResult writes a typed Value to the first write-slot and advances PC.
func writeResult(f *rwir.Frame, result kvspace.XValue) error {
	if len(f.Inst.Writes) > 0 {
		key := resolveWriteSlot(f.KV, keytree.FrameRoot(f.PC), f.Inst.Writes[0].Name)
		if err := f.KV.Set([]kvspace.KVPair{{key, result}}); err != nil {
			return err
		}
	}
	vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
	return nil
}

// resolveWriteSlot 返回写槽的绝对 KV key（先查 .wparam 重定向，再处理绝对路径）。
func resolveWriteSlot(kv kvspace.KVSpace, framePath, name string) string {
	if isAbsolute(name) { return name }
	rwRoot := funcFrameRoot(kv, framePath)
	if r := kvspace.GetOne(kv, keytree.WParam(rwRoot, name)); !r.IsNone() {
		return r.Str()
	}
	return keytree.Stack(rwRoot) + name
}

// nextPC advances PC without writing a result.
func nextPC(f *rwir.Frame) {
	vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
}

// setWrite writes a typed Value to a named slot (先查 .wparam 重定向).
func setWrite(kv kvspace.KVSpace, framePath, slot string, val kvspace.XValue) error {
	return kv.Set([]kvspace.KVPair{{resolveWriteSlot(kv, framePath, slot), val}})
}

// funcFrameRoot returns the nearest rwfunc frame root from the given frame path.
// Uses .lib marker to identify rwfunc frames (extKind fails due to extindex cascade).
func funcFrameRoot(kv kvspace.KVSpace, frameRoot string) string {
	for f := frameRoot; f != ""; f = keytree.ParentFrame(f) {
		if !kvspace.GetOne(kv, keytree.Stack(f)+keytree.SegLib).IsNone() {
			return f
		}
	}
	return frameRoot
}

// ExecuteCopy copies the Value addressed by inst.Reads[0] to all write-slots (先查 .wparam 重定向).
// Preserves the original type — int stays int, float stays float.
func ExecuteCopy(kv kvspace.KVSpace, vtid, pc string, inst *rwir.Rwir) error {
	framePath := keytree.FrameRoot(pc)
	var src string
	if len(inst.Reads) > 0 { src = inst.Reads[0].Name }
	v := resolveReadValue(kv, framePath, src)
	for _, w := range inst.Writes {
		key := resolveWriteSlot(kv, framePath, w.Name)
		if err := kv.Set([]kvspace.KVPair{{key, v}}); err != nil {
			return err
		}
	}
	vthread.Set(bg, kv, vtid, rwir.NextPC(pc), "running")
	return nil
}
