// Package ast 定义 kvlang 抽象语法树节点类型。
package ast

import (
	"context"
	"fmt"

	"github.com/redis/go-redis/v9"
)

// Func 表示一个函数定义。
type Func struct {
	Name      string   // 函数名
	Signature string   // def funcName(A:int, B:int) -> (C:int)
	Body      []string // kvlang 指令行
}

// Register 将函数定义写入 Redis KV 空间。
func (fn *Func) Register(ctx context.Context, rdb *redis.Client) error {
	if err := rdb.Set(ctx, "/src/func/"+fn.Name, fn.Signature, 0).Err(); err != nil {
		return fmt.Errorf("register sig: %w", err)
	}
	for i, line := range fn.Body {
		key := fmt.Sprintf("/src/func/%s/%d", fn.Name, i)
		if err := rdb.Set(ctx, key, line, 0).Err(); err != nil {
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
