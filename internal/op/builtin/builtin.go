// Package builtin 提供 VM 内建算子求值引擎。
package builtin

import (
	"context"
	"fmt"

	"github.com/array2d/kvlang-go"
	"kvlang/internal/op"
)

// Native 内建算子入口：Dispatch → Call。
func Native(ctx context.Context, kv kvspace.KVSpace, vtid string, pc string, inst *op.Instruction) error {
	o, ok := Dispatch(inst.Opcode)
	if !ok {
		return fmt.Errorf("unknown builtin op: %s", inst.Opcode)
	}
	f := &op.Frame{KV: kv, Vtid: vtid, PC: pc, Inst: inst}
	return o.Call(f)
}
