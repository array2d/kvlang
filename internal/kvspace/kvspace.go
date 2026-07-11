// Package kvspace 抽象 KV 存储。
package kvspace

import "time"

// KVSpace KV 存储接口。
//
// 生命周期：Conn(dsn) → Set/Get/Del/... → DisConn()
type KVSpace interface {
	// 读
	Get(key string) (string, error)          // 单 key；不存在返回 ("", redis.Nil)
	Gets(keys ...string) ([]string, error)   // 多 key；缺失位置为 ""

	// 写
	Set(key string, value any) error         // 单 key 写入（自动维护目录索引）
	Sets(kvs map[string]any) error           // 批量写入

	// 删
	Del(keys ...string) error                // 精确删除（含索引清理）
	DelR(prefix string) error                // 递归删除 prefix 及其所有子项

	// 目录
	List(prefix string) ([]string, error)    // 列出 prefix 的直接子项名

	// 消息
	Watch(key string, timeout time.Duration) (string, error) // 阻塞等待单 key 消息
	Notify(key string, value any) error                      // 推送一条消息

	// 连接
	DisConn() error
}
