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

	"github.com/array2d/kvspace-go"
	"kvlang/internal/keytree"
)

// ResolveTerm 通过 /vthread/<vtid>/term → /sys/term/${name}/${stream} 解析终端流配置。
func ResolveTerm(ctx context.Context, kv kvspace.KVSpace, vtid, stream string) TermStream {
	nameVal := kvspace.GetOne(kv, keytree.VThreadTerm(vtid))
	name := nameVal.Str()
	if name == "" { return TermStream{} }
	base := keytree.DevTTY(name, stream)
	tVal := kvspace.GetOne(kv, base+"/type")
	dVal := kvspace.GetOne(kv, base+"/detail")
	return TermStream{Type: tVal.Str(), Detail: dVal.Str()}
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
