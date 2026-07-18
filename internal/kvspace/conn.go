package kvspace

import (
	"fmt"
	"sort"
	"strings"
)

// Factory 是 KVSpace 实现的构造函数类型；addr 为去除 scheme 后的地址。
type Factory func(addr string, poolSize int) KVSpace

var registry = map[string]Factory{}

// Register 注册一个 scheme 对应的 KVSpace 实现。在实现包的 init() 中调用。
func Register(scheme string, f Factory) { registry[scheme] = f }

// Conn 用默认连接池（16）创建 KVSpace。
func Conn(dsn string) KVSpace { return ConnPool(dsn, 16) }

// ConnPool 创建带指定连接池大小的 KVSpace。
//
// dsn 形如 scheme://addr（如 redis://127.0.0.1:6379），scheme 即注册的后端名；
// 裸 addr（无 scheme）视为 redis。
// 需在 main 中空白导入对应实现包以触发 init() 注册，例如：
//
//	import _ "kvlang/internal/kvspace/redis"
func ConnPool(dsn string, poolSize int) KVSpace {
	scheme, addr := "redis", dsn
	if i := strings.Index(dsn, "://"); i >= 0 {
		scheme, addr = dsn[:i], dsn[i+3:]
	}
	f, ok := registry[scheme]
	if !ok {
		names := make([]string, 0, len(registry))
		for k := range registry {
			names = append(names, k)
		}
		sort.Strings(names)
		panic(fmt.Sprintf("kvspace: unknown scheme %q in dsn %q; registered: %v", scheme, dsn, names))
	}
	return f(addr, poolSize)
}
