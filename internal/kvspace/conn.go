package kvspace

import (
	"fmt"
	"os"
	"sort"
)

// Factory 是 KVSpace 实现的构造函数类型。
type Factory func(dsn string, poolSize int) KVSpace

var registry = map[string]Factory{}

// Register 注册一个命名的 KVSpace 实现。在实现包的 init() 中调用。
func Register(name string, f Factory) { registry[name] = f }

// Conn 用默认连接池（16）创建 KVSpace。
// 实现由 KVLANG_KV 环境变量选择（默认 "redis"）。
func Conn(dsn string) KVSpace { return ConnPool(dsn, 16) }

// ConnPool 创建带指定连接池大小的 KVSpace。
// 实现由 KVLANG_KV 环境变量选择（默认 "redis"）。
// 需在 main 中空白导入对应实现包以触发 init() 注册，例如：
//
//	import _ "kvlang/internal/kvspace/redis"
func ConnPool(dsn string, poolSize int) KVSpace {
	name := os.Getenv("KVLANG_KV")
	if name == "" {
		name = "redis"
	}
	f, ok := registry[name]
	if !ok {
		names := make([]string, 0, len(registry))
		for k := range registry {
			names = append(names, k)
		}
		sort.Strings(names)
		panic(fmt.Sprintf("kvspace: unknown backend %q; registered: %v", name, names))
	}
	return f(dsn, poolSize)
}
