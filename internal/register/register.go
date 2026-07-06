// Package register 提供函数定义到 Redis KV 空间的注册逻辑。
package register

import (
	"context"
	"fmt"

	"kvlang/internal/ast"
	"github.com/redis/go-redis/v9"
)

// Func 将函数定义注册到 Redis：/src/func/<name> = 签名，/src/func/<name>/<i> = 指令行。
func Func(ctx context.Context, rdb *redis.Client, fn *ast.Func) error {
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
