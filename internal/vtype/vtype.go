// Package vtype 定义 kvlang 的值类型（Value Type）命名空间体系。
//
// 每个 VType 拥有一个 dot-prefix，其操作码格式为 "<vtype>.<op>"，例如：
//   tensor.add   tensor.new   str.set   str.concat
//
// 调用方通过 Lookup(opcode) 查找对应 VType，再调用 Exec 执行。
// 这使 execute.go 的 dispatch switch 无需任何 KV 查询即可路由 vtype 操作。
package vtype

import (
	"context"
	"strings"

	"kvlang/internal/kvspace"
	"kvlang/internal/op"
)

// VType 表示一种 kvlang 值类型，拥有独立的操作命名空间。
type VType interface {
	// Name 返回命名空间前缀，如 "tensor"、"str"。
	Name() string
	// Exec 执行 <vtype>.<op> 指令。
	Exec(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error
}

var registry = map[string]VType{}

// Register 注册一个 VType 实现（通常在 init() 中调用）。
func Register(vt VType) { registry[vt.Name()] = vt }

// Lookup 根据 opcode 前缀查找 VType。
//   "tensor.add"  → tensor VType
//   "str.set"     → str VType
//   "print"       → nil（无 dot 或未注册）
func Lookup(opcode string) VType {
	dot := strings.IndexByte(opcode, '.')
	if dot <= 0 {
		return nil
	}
	return registry[opcode[:dot]]
}

// OpName 去掉命名空间前缀，返回裸操作名。
//   "tensor.matmul" → "matmul"
//   "str.set"       → "set"
//   "print"         → "print"
func OpName(opcode string) string {
	dot := strings.IndexByte(opcode, '.')
	if dot < 0 {
		return opcode
	}
	return opcode[dot+1:]
}
