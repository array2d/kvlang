// Package kvspace 抽象 KV 存储。
package kvspace

import "time"

// KVSpace KV 存储接口。
//
// 生命周期：Conn(dsn) → Set/Get/Del/... → DisConn()
type KVSpace interface {
	// 基础 KV
	Get(key string) (string, error)
	Set(key string, value any, ttl time.Duration) error
	Del(keys ...string) error
	MGet(keys ...string) ([]any, error)

	// 目录
	List(prefix string) ([]string, error) // 列出 prefix 下的直接子项

	// 通知
	Watch(timeout time.Duration, keys ...string) ([]string, error)
	Notify(key string, values ...any) error

	// 连接
	DisConn() error

}
