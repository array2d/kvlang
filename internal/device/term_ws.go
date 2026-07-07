// Package device 提供终端 I/O 传输层  终端 I/O 传输层（WebSocket + 文件）。
//
// 终端发现流程：
//   /vthread/<vtid>/term → 终端名称 $name（默认空字符串，空则无终端）
//   /sys/term/${name}/stdout  → HASH {type, detail}
//   /sys/term/${name}/stderr  → HASH {type, detail}
//   /sys/term/${name}/stdin   → HASH {type, detail}
//
// type 取值: "websocket" | "file"
// detail: ws://url 或文件路径
//
// 不做任何序列化，直接传原始字节流。
package device

import (
	"context"

	"github.com/redis/go-redis/v9"
	"kvlang/internal/keytree"
)

// ResolveTerm 通过 /vthread/<vtid>/term → /sys/term/${name}/${stream} 解析终端流配置。
func ResolveTerm(ctx context.Context, rdb *redis.Client, vtid, stream string) TermStream {
	name, err := rdb.Get(ctx, keytree.VThreadTerm(vtid)).Result()
	if err != nil || name == "" {
		return TermStream{}
	}
	key := keytree.SysTerm(name, stream)
	results, err := rdb.HMGet(ctx, key, "type", "detail").Result()
	if err != nil || len(results) < 2 {
		return TermStream{}
	}
	var ts TermStream
	if t, ok := results[0].(string); ok {
		ts.Type = t
	}
	if d, ok := results[1].(string); ok {
		ts.Detail = d
	}
	return ts
}

// WriteTerm 根据 TermStream 类型将文本写入终端。
func WriteTerm(ctx context.Context, s TermStream, text string) error {
	switch s.Type {
	case "websocket":
		return writeWS(ctx, s.Detail, text)
	case "file":
		return writeFile(s.Detail, text)
	default:
		return nil // 无终端，静默丢弃
	}
}

// ReadTerm 根据 TermStream 类型从终端读取一行文本。
func ReadTerm(ctx context.Context, s TermStream) (string, error) {
	switch s.Type {
	case "websocket":
		return readWS(ctx, s.Detail)
	case "file":
		return readFile(s.Detail)
	default:
		return "", nil // 无终端，返回空
	}
}
