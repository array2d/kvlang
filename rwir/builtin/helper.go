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
		inputs = append(inputs, resolveReadValue(f.KV, framePath, r))
	}
	return inputs
}

// writeResult writes a typed Value to the first write-slot and advances PC.
func writeResult(f *rwir.Frame, result kvspace.XValue) error {
	if len(f.Inst.Writes) > 0 {
		key := resolveWriteSlot(f.KV, keytree.FrameRoot(f.PC), f.Inst.Writes[0].Name)
		if err := f.KV.Set([]kvspace.KVPair{{key, result, -1}}); err != nil {
			return err
		}
	}
	vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
	return nil
}

// resolveWriteSlot 返回写槽的绝对 KV key。
// Ptr → slot → Char(path) 链，沿着 Char 一路解引用到底（非 Char 类型），
// 防止递归调用时中间帧的 slot 里 Char(path) 被返回值覆盖。
func resolveWriteSlot(kv kvspace.KVSpace, framePath, name string) string {
	if isAbsolute(name) { return name }
	rwRoot := funcFrameRoot(kv, framePath)
	if ptrVal := kvspace.GetOne(kv, keytree.Stack(rwRoot)+name); kvspace.IsPtr(ptrVal) {
		argAddr := kvspace.GetOne(kv, keytree.Stack(rwRoot)+kvspace.PtrTarget(ptrVal))
		if !kvspace.IsNone(argAddr) {
			v := argAddr
			for v.Kind() == kvspace.KindString {
				next := kvspace.GetOne(kv, v.ValueString())
				if kvspace.IsNone(next) || next.Kind() != kvspace.KindString {
					return v.ValueString()
				}
				v = next
			}
			return v.ValueString()
		}
	}
	return keytree.Stack(rwRoot) + name
}

// nextPC advances PC without writing a result.
func nextPC(f *rwir.Frame) {
	vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
}

// setWrite writes a typed Value to a named slot (先查 .wparam 重定向).
func setWrite(kv kvspace.KVSpace, framePath, slot string, val kvspace.XValue) error {
	return kv.Set([]kvspace.KVPair{{resolveWriteSlot(kv, framePath, slot), val, -1}})
}

// isContainerKind reports whether a kind represents a container/marker type
// (dict, index, etc.) whose String() method does not return a path.
func isContainerKind(kind string) bool {
	switch kind {
	case "dict", "index", "extindex", "rwfunc", "rwir":
		return true
	}
	return false
}

// resolveBasePath resolves the first read-slot of an at/has/set instruction as a KV base path.
// For container types (dict, index, etc.) and None, resolves the variable name from the function frame.
// For string values starting with "/", uses the string directly as a path.
func resolveBasePath(kv kvspace.KVSpace, fp, funcFrame string, read rwir.Param) string {
	v := resolveReadValue(kv, fp, read)
	if kvspace.IsNone(v) || isContainerKind(v.Kind()) {
		return resolveKVPath(funcFrame, read.Name)
	}
	return v.ValueString()
}

// funcFrameRoot returns the nearest rwfunc frame root from the given frame path.
// Uses .lib marker to identify rwfunc frames (extKind fails due to extindex cascade).
func funcFrameRoot(kv kvspace.KVSpace, frameRoot string) string {
	for f := frameRoot; f != ""; f = keytree.ParentFrame(f) {
		if !kvspace.IsNone(kvspace.GetOne(kv, keytree.Stack(f)+keytree.SegLib)) {
			return f
		}
	}
	return frameRoot
}

// ExecuteCopy copies the Value addressed by inst.Reads[0] to all write-slots (先查 .wparam 重定向).
// Preserves the original type — int stays int, float stays float.
func ExecuteCopy(kv kvspace.KVSpace, vtid, pc string, inst *rwir.Rwir) error {
	framePath := keytree.FrameRoot(pc)
	if len(inst.Reads) == 0 {
		vthread.Set(bg, kv, vtid, rwir.NextPC(pc), "running")
		return nil
	}
	v := resolveReadValue(kv, framePath, inst.Reads[0])
	for _, w := range inst.Writes {
		key := resolveWriteSlot(kv, framePath, w.Name)
		if err := kv.Set([]kvspace.KVPair{{key, v, -1}}); err != nil {
			return err
		}
	}
	vthread.Set(bg, kv, vtid, rwir.NextPC(pc), "running")
	return nil
}
