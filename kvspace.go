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
//	v := kv.Get("/vt/0", []string{"pc"})[0]; pc := v.Str()
//	kv.Notify("/vt/0/status", kvspace.Str("running"))
//	val, _ := kv.Watch("/vt/0/status", 5*time.Second)
//
// Watch/Notify 语义：监听单个 key 的值变化通知，不是通用消息队列。
//
//	Notify(key, val) 向等待者投递 val；不等价于 Set（不写持久值）。
//	Watch(key, timeout) 阻塞等待下一次 Notify；超时返回 (Value{}, ErrNotFound)。
//
// 软链接透明穿透：Link(target, linkpath) 后，访问 linkpath/x 透明地访问 target/x。
// 删除语义例外（POSIX rm 式）：Del/DelTree/Unlink 的最终组件作用于链接本体，
// 不穿透 target；路径中的祖先链接仍穿透（Del("/alias/x") 删 /real/x）。
type KVSpace interface {
	// ── 单点读写 ─────────────────────────────────────────────────────────
	Get(prefix string, keys []string) []XValue //所有key共享prefix，key不含/；缺失返回xvalue（kind=null）
	Set(pairs []KVPair) error                  // 写入并维护目录索引,pre路径如果不存在，则汇报异常

	// ── 目录操作 ─────────────────────────────────────────────────────────
	List(prefix string, expandExt bool) []string // 列出直接子项名；expandExt 合并 extindex 子项
	Del(keys ...string) error                    // 精确删除（含索引清理）
	DelTree(prefix string) error                 // 递归删除；prefix 本身是链接则只删链接

	// ── 变更通知 ─────────────────────────────────────────────────────────
	Notify(key string, val XValue) error            // 投递一次性通知信号
	Watch(key string, timeout time.Duration) XValue // 阻塞等待通知

	// ── 目录创建 ─────────────────────────────────────────────────────────
	Mkindex(path string) error // 递归创建目录，类似 mkdir -p；path 须以 / 结尾

	// ── mount系统 ───────────────────────────────────────────────────────────
	Link(target, linkpath string) error  // 创建路径映射 linkpath → target，纯链接
	ExtIndex(path, extpath string) error // 创建扩展索引，path 为写层，extpath 为只读扩展
	UnLink(path string) error            // 移除 extindex

	// ── 生命周期 ─────────────────────────────────────────────────────────
	// 范围警示：redis 实现 = FLUSHDB，清空所在 db 的全部键——共享 Redis 实例时会波及非 kvlang 数据。
	Clear() error
	DisConn() error
}
