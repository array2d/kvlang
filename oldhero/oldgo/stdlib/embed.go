// Package stdlib 内置标准库的 kvlang 源码，构建时 //go:embed 打包进 runtime 二进制。
package stdlib

import "embed"

//go:embed *.kv
var FS embed.FS
