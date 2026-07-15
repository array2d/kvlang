package builtin

import (
	"fmt"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

func requireBinary(inputs []kvspace.Value) error {
	if len(inputs) != 2 { return fmt.Errorf("binary op requires 2 inputs, got %d", len(inputs)) }
	return nil
}
func requireUnary(inputs []kvspace.Value) error {
	if len(inputs) != 1 { return fmt.Errorf("unary op requires 1 input, got %d", len(inputs)) }
	return nil
}

// readInputs resolves all read-slots of f.Inst into typed Values.
func readInputs(f *op.Frame) []kvspace.Value {
	framePath := keytree.FrameRoot(f.PC)
	inputs := make([]kvspace.Value, 0, len(f.Inst.Reads))
	for _, r := range f.Inst.Reads {
		inputs = append(inputs, resolveReadValue(f.KV, framePath, r))
	}
	return inputs
}

// writeResult writes a typed Value to the first write-slot and advances PC.
func writeResult(f *op.Frame, result kvspace.Value) error {
	if len(f.Inst.Writes) > 0 {
		outKey := resolveWriteKey(keytree.FrameRoot(f.PC), f.Inst.Writes[0])
		if err := f.KV.Set(outKey, result); err != nil { return err }
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

// nextPC advances PC without writing a result.
func nextPC(f *op.Frame) {
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
}

// ExecuteCopy copies the Value addressed by inst.Opcode to all write-slots.
// Preserves the original type — int stays int, float stays float.
func ExecuteCopy(kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	framePath := keytree.FrameRoot(pc)
	v := resolveReadValue(kv, framePath, inst.Opcode)
	for _, w := range inst.Writes {
		if err := kv.Set(resolveWriteKey(framePath, w), v); err != nil {
			return err
		}
	}
	vthread.Set(bg, kv, vtid, op.NextPC(pc), "running")
	return nil
}
