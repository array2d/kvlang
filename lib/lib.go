// Package lib 提供 lib 内置 key（可读写数据，如 math.Pi）的注册与 /lib/ 布局。
package lib

import (
	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
)

// libdata 记录 lib 包注册的内置 key（可读写数据，如 math.Pi），写入 /lib/。
var libdata = map[string]kvspace.XValue{}

// RegisterLibData 注册 lib 内置 key（常量等可读写数据），写入 /lib/<name>。
func RegisterLibData(name string, val kvspace.XValue) {
	libdata[name] = val
}

// WriteLibData 将 lib 内置 key（可读写数据）写入 /lib/。
// /lib/ 下只放两类内容：可执行（rwfunc，有指令体）或可读写（数据/常量）。
func WriteLibData(kv kvspace.KVSpace) {
	pairs := make([]kvspace.KVPair, 0, len(libdata))
	for name, val := range libdata {
		pairs = append(pairs, kvspace.KVPair{Key: keytree.LibRoot + keytree.PathSegSep + name, Val: val})
	}
	if len(pairs) > 0 {
		kv.Set(pairs)
	}
}
