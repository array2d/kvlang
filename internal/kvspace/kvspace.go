// Package kvspace 抽象 KV 存储。
package kvspace

import "time"

// KVPair 用于批量写入，顺序确定（非 map）。
type KVPair struct {
	Key string
	Val XValue
}

// KVSpace KV 存储接口。
//
// 使用模式：
//
//	kv.Set("/vt/0/pc", kvspace.Str("init/[0,0]"))
//	v, _ := kv.Get("/vt/0/pc"); pc := v.Str()
//	kv.Notify("/vt/0/status", kvspace.Str("running"))
//	val, _ := kv.Watch("/vt/0/status", 5*time.Second)
//
// Watch/Notify 语义：监听单个 key 的值变化通知，不是通用消息队列。
//
//	Notify(key, val) 向等待者投递 val；不等价于 Set（不写持久值）。
//	Watch(key, timeout) 阻塞等待下一次 Notify；超时返回 (Value{}, ErrNotFound)。
//
// 软链接透明穿透：Link(target, linkpath) 后，访问 linkpath/x 透明地访问 target/x。
type KVSpace interface {
	// ── 单点读写 ─────────────────────────────────────────────────────────
	Get(key string) (XValue, error)   // key 不存在返回 (Value{}, ErrNotFound)
	Set(key string, val XValue) error // 写入并维护目录索引
	Del(keys ...string) error        // 精确删除（含索引清理）

	// ── 批量读写 ─────────────────────────────────────────────────────────
	GetMany(keys []string) ([]XValue, error) // 缺失位置返回 Value{}，不返回 error
	SetMany(pairs []KVPair) error           // 批量写入

	// ── 目录操作 ─────────────────────────────────────────────────────────
	List(prefix string) ([]string, error) // 列出直接子项名
	DelTree(prefix string) error          // 递归删除；prefix 本身是链接则只删链接

	// ── 变更通知 ─────────────────────────────────────────────────────────
	Notify(key string, val XValue) error                    // 投递一次性通知信号
	Watch(key string, timeout time.Duration) (XValue, error) // 阻塞等待通知

	// ── 软链接 ───────────────────────────────────────────────────────────
	Link(target, linkpath string) error // 创建软链接：linkpath → target
	Unlink(linkpath string) error       // 删除链接本身（不影响 target）

	// ── 生命周期 ─────────────────────────────────────────────────────────
	DisConn() error
}
