// Package kvspace 抽象 KV 存储。
package kvspace

import "time"

// KVSpace KV 存储接口。
// 软链接：Link(target, linkpath) 后，访问 linkpath/x 透明地访问 target/x。
type KVSpace interface {
	Get(key string) (string, error)                          // 单 key；不存在返回 ("", redis.Nil)
	Gets(keys ...string) ([]string, error)                   // 多 key；缺失位置为 ""
	Set(key string, value any) error                         // 写入（自动维护目录索引）
	Sets(kvs map[string]any) error                           // 批量写入
	Del(keys ...string) error                                // 精确删除（含索引清理）
	DelR(prefix string) error                                // 递归删除；prefix 本身是链接则只删链接
	List(prefix string) ([]string, error)                    // 列出直接子项名
	Watch(key string, timeout time.Duration) (string, error) // 阻塞等待消息（BLPOP）
	Notify(key string, value any) error                      // 推送消息（LPUSH）
	Link(target, linkpath string) error                      // 创建软链接：linkpath → target
	Unlink(linkpath string) error                            // 删除链接本身（不影响 target）
	DisConn() error
}
