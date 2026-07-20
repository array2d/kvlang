// Package vtype 定义 kvlang 的值类型（Value Type）命名空间体系。
// Kind 常量是所有 vtype name 的权威来源，供全局使用。
//
// 每个 VType 拥有一个 dot-prefix，其操作码格式为 "<vtype>.<op>"，例如：
//   tensor.add   tensor.new   string.set   string.concat
//
// 调用方通过 Lookup(opcode) 查找对应 VType，再调用 Exec 执行。
// 这使 execute.go 的 dispatch switch 无需任何 KV 查询即可路由 vtype 操作。
package vtype

import (
	"context"
	"strings"

	"github.com/array2d/kvspace-go"
	"kvlang/internal/op"
)

// VType 表示一种 kvlang 值类型，拥有独立的操作命名空间。
type VType interface {
	// Name 返回命名空间前缀，如 "tensor"、"str"。
	Name() string
	// Exec 执行 <vtype>.<op> 指令。
	Exec(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error
}

// ── Kind 常量（所有 vtype name 的权威来源）──────────────────────────────────
// kvspace.XValue.Kind() 与这些常量对齐；kvspace 包因循环导入限制使用字符串字面量，
// 其余所有包统一引用这里的常量。
const (
	KindInt    = "int"
	KindFloat  = "float"
	KindBool   = "bool"
	KindStr    = "string"
	KindBytes  = "bytes"
	KindTensor = "tensor"
)

var registry = map[string]VType{}

// Register 注册一个 VType 实现（通常在 init() 中调用）。
func Register(vt VType) { registry[vt.Name()] = vt }

// Lookup 根据 opcode 前缀查找 VType。
//   "tensor.add"   → tensor VType
//   "string.set"   → string VType
//   "print"        → nil（无 dot 或未注册）
func Lookup(opcode string) VType {
	dot := strings.IndexByte(opcode, '.')
	if dot <= 0 {
		return nil
	}
	return registry[opcode[:dot]]
}

// OpName 去掉命名空间前缀，返回裸操作名。
//   "tensor.matmul"  → "matmul"
//   "string.set"     → "set"
//   "print"          → "print"
func OpName(opcode string) string {
	dot := strings.IndexByte(opcode, '.')
	if dot < 0 {
		return opcode
	}
	return opcode[dot+1:]
}
