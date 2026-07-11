package vtype

import (
	"context"
	"fmt"

	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/op/dispatch"
	"kvlang/internal/vthread"
)

// tensorVType 处理 tensor.* 操作码，调度到外部后端进程执行。
//
// 路由规则（按裸操作名）：
//   tensor.new / tensor.del / tensor.clone  →  heap-plat（内存管理）
//   其他 tensor.*                           →  op-plat（计算）
type tensorVType struct{}

func (tensorVType) Name() string { return "tensor" }

func (t tensorVType) Exec(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	name := OpName(inst.Opcode) // strip "tensor."
	switch name {
	case "new", "del", "clone":
		return dispatch.Lifecycle(ctx, kv, vtid, pc, inst)
	default:
		if _, err := dispatch.Select(ctx, kv, inst.Opcode); err != nil {
			msg := fmt.Sprintf("no backend for %s: %v", inst.Opcode, err)
			vthread.SetError(ctx, kv, vtid, pc, msg)
			return fmt.Errorf("%s", msg)
		}
		return dispatch.Compute(ctx, kv, vtid, pc, inst)
	}
}

func init() { Register(tensorVType{}) }
