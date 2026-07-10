// Package cache 提供 kvcache：子栈指令的本地内存缓存，避免每条指令都访问 kvspace。
package cache

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/kvspace"
)

// KVCache 子栈指令本地缓存。
// CALL 翻译后加载，RETURN 时释放。
type KVCache struct {
	Prefix string            // keytree.VThreadSub("1", "[2,0]")
	KVs    map[string]string // 相对 key → value, e.g., "[0,0]"→"matmul"
}

// NewKVCache 从 kvspace MGET 加载整个子栈到本地。
func NewKVCache(ctx context.Context, kv kvspace.KVSpace, prefix string) *KVCache {

	children, err := kv.List(prefix)
	if err != nil || len(children) == 0 {
		return nil
	}
	keys := make([]string, len(children))
	for i, c := range children {
		keys[i] = prefix + "/" + c
	}

	vals, err := kv.MGet(keys...)
	if err != nil {
		return nil
	}

	c := &KVCache{
		Prefix: prefix,
		KVs:    make(map[string]string, len(keys)),
	}

	for i, key := range keys {
		localKey := strings.TrimPrefix(key, prefix)
		if s, ok := vals[i].(string); ok {
			c.KVs[localKey] = s
		}
	}

	return c
}

// Get 从本地缓存读取指令坐标的值。
func (c *KVCache) Get(addr0, addr1 int) string {
	return c.KVs[fmt.Sprintf("[%d,%d]", addr0, addr1)]
}
