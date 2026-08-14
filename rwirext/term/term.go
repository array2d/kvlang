// Package term 提供终端 I/O 传输层（文件）。
//
// 终端发现流程：
//   /vthread/<vtid>/term → 终端名称 $name（默认空字符串，空则无终端）
//   /sys/term/${name}/stdout  → {type, detail}
//   /sys/term/${name}/stderr  → {type, detail}
//   /sys/term/${name}/stdin   → {type, detail}
//
// type 取值: "file"
// detail: 文件路径
//
// 不做任何序列化，直接传原始字节流。
package term

import (
	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
)

// TermStream 表示一个已解析的终端流配置。
type TermStream struct {
	Type   string // "file" | ""
	Detail string // 文件路径
}

// IsZero 终端未配置时返回 true。
func (s TermStream) IsZero() bool { return s.Type == "" }

// ResolveTerm 通过 /vthread/<vtid>/term → /sys/term/${name}/${stream} 解析终端流配置。
func ResolveTerm(kv kvspace.KVSpace, vtid, stream string) TermStream {
	name := kvspace.GetOne(kv, keytree.VThreadTerm(vtid)).ValueString()
	if name == "" {
		return TermStream{}
	}
	base := keytree.DevTTY(name, stream)
	return TermStream{
		Type:   kvspace.GetOne(kv, base+"/type").ValueString(),
		Detail: kvspace.GetOne(kv, base+"/detail").ValueString(),
	}
}

// WriteTerm 根据 TermStream 类型将文本写入终端（追加换行）。
func WriteTerm(s TermStream, text string) error {
	if s.Type == "file" {
		return writeFile(s.Detail, text)
	}
	return nil
}

// WriteTermRaw 同 WriteTerm 但不追加换行。
func WriteTermRaw(s TermStream, text string) error {
	if s.Type == "file" {
		return writeFileRaw(s.Detail, text)
	}
	return nil
}

// ReadTerm 根据 TermStream 类型从终端读取一行文本。
func ReadTerm(s TermStream) (string, error) {
	if s.Type == "file" {
		return readFile(s.Detail)
	}
	return "", nil
}
