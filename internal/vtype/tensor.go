package vtype

import (
	"context"
	"fmt"

	"github.com/array2d/kvspace-go"
	"kvlang/internal/op"
	"kvlang/internal/op/dispatch"
	"kvlang/internal/vthread"
)

// tensorVType 处理 tensor.* 操作码，统一调度到 /sys/op/<backend>/<n>/cmd。
type tensorVType struct{}

func (tensorVType) Name() string { return "tensor" }

func (t tensorVType) Exec(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	if _, _, err := dispatch.Select(ctx, kv, inst.Opcode); err != nil {
		msg := fmt.Sprintf("no backend for %s: %v", inst.Opcode, err)
		vthread.SetError(ctx, kv, vtid, pc, msg)
		return fmt.Errorf("%s", msg)
	}
	return dispatch.Compute(ctx, kv, vtid, pc, inst)
}

func init() { Register(tensorVType{}) }
