package builtin

import (
	"context"
	"fmt"

	"kvlang/op"

	"github.com/array2d/kvspace-go"
)

// Op 内建算子接口。
type Op interface {
	Call(f *op.Frame) error
}

// Dispatch 根据 opcode 分发算子。
func Dispatch(opcode string) (Op, bool) {
	o, ok := registry[opcode]
	return o, ok
}

// Native 内建算子入口：Dispatch → Call。
func Native(ctx context.Context, kv kvspace.KVSpace, vtid string, pc string, inst *op.Instruction) error {
	o, ok := Dispatch(inst.Opcode)
	if !ok {
		return fmt.Errorf("unknown builtin op: %s", inst.Opcode)
	}
	f := &op.Frame{KV: kv, Vtid: vtid, PC: pc, Inst: inst}
	return o.Call(f)
}
