// Package kvspace 抽象 KV 存储。
package kvspace

import "time"

// KVSpace KV 存储接口。
type KVSpace interface {
	// ── 保留 ──

	Get(key string) (string, error)
	Set(key string, value any, ttl time.Duration) error
	Del(keys ...string) error
	MGet(keys ...string) ([]any, error) // 批量 Get
	Watch(timeout time.Duration, keys ...string) ([]string, error)
	Notify(key string, values ...any) error
	DisConn() error

	// ── 待改造 ──


	// TODO Keys → 调用方自维护索引，不再依赖 pattern scan
	Keys(pattern string) ([]string, error)

	// TODO Incr → Get + Set 自增
	Incr(key string) (int64, error)

	// TODO RPush → 归入 Notify
	RPush(key string, values ...any) error

	// TODO LRange → 独立 key 存储 op 列表，用 Get 读取
	LRange(key string, start, stop int64) ([]string, error)

	// TODO HMGet → 多次 Get
	HMGet(key string, fields ...string) ([]any, error)

	// TODO Eval → Set + 乐观锁 (先 Get 判 init 再 Set)
	Eval(script string, keys []string, args ...any) (int64, error)

}
