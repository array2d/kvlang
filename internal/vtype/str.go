package vtype

import (
	"context"
	"fmt"

	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

// strVType 处理 str.* 操作码，在 VM 进程内执行（委托 builtin 求值器）。
// 当前支持的操作：str.set
// 未来可扩展：str.concat、str.len、str.slice、str.fmt 等
type strVType struct{}

func (strVType) Name() string { return "str" }

func (s strVType) Exec(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	vthread.SetError(ctx, kv, vtid, pc, fmt.Sprintf("unknown str op: %s", inst.Opcode))
	return fmt.Errorf("unknown str op: %s", inst.Opcode)
}

func init() { Register(strVType{}) }
