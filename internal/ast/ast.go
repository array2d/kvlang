// Package ast 定义 kvlang 抽象语法树节点类型。
package ast

import (
	"context"
	"fmt"

	"kvlang/internal/kvspace"
	"kvlang/internal/keytree"
)

// Func 表示一个函数定义。
type Func struct {
	Name      string   // 函数名
	Signature string   // def funcName(A:int, B:int) -> (C:int)
	Body      []string // kvlang 指令行
}

// Register 将函数定义写入 kvspace 空间。
func (fn *Func) Register(ctx context.Context, kv kvspace.KVSpace) error {
	if err := kv.Set(keytree.SrcFunc(fn.Name), fn.Signature, 0); err != nil {
		return fmt.Errorf("register sig: %w", err)
	}
	for i, line := range fn.Body {
		key := keytree.SrcFuncLine(fn.Name, i)
		if err := kv.Set(key, line, 0); err != nil {
			return fmt.Errorf("register body[%d]: %w", i, err)
		}
	}
	return nil
}

// Instruction 表示解析后的单条 kvlang 指令。
type Instruction struct {
	Opcode string   // 操作码
	Reads  []string // 输入参数
	Writes []string // 输出槽位
}

// FormalParams 表示函数签名的形参列表。
type FormalParams struct {
	Reads  []string
	Writes []string
}
