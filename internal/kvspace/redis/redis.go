// Package redis 提供 kvspace.KVSpace 的 Redis 实现。
package redis

import (
	"context"
	"errors"
	"strings"
	"sync"
	"time"

	goredis "github.com/redis/go-redis/v9"
	"kvlang/internal/kvspace"
)

func init() {
	kvspace.Register("redis", ConnPool)
}

var bg = context.Background()

// linkSentinel 软链接存储前缀：value 以 "->" 开头表示链接目标。
const linkSentinel = "->"

// Conn 使用默认连接池（poolSize=16）创建 KVSpace。
func Conn(dsn string) kvspace.KVSpace { return ConnPool(dsn, 16) }

// ConnPool 创建带自定义连接池的 KVSpace。
// serve 模式建议 poolSize = workers+16。
func ConnPool(dsn string, poolSize int) kvspace.KVSpace {
	if poolSize < 16 {
		poolSize = 16
	}
	return &redisImpl{
		rdb: goredis.NewClient(&goredis.Options{
			Addr:         dsn,
			PoolSize:     poolSize,
			MinIdleConns: min(poolSize/4, 8),
			PoolTimeout:  10 * time.Second,
			ReadTimeout:  3 * time.Second,
			WriteTimeout: 3 * time.Second,
		}),
		links: make(map[string]linkEntry),
	}
}

// linkEntry 记录路径的链接检查结果。
//
//	{checked:false}         零值，尚未检查（lazy GET）
//	{checked:true, target:""}   确认非链接（否定缓存）
//	{checked:true, target:"x"}  链接，目标路径为 "x"
type linkEntry struct {
	checked bool
	target  string
}

type redisImpl struct {
	rdb    *goredis.Client
	linkMu sync.RWMutex
	links  map[string]linkEntry
}

// ── 软链接 ───────────────────────────────────────────────────────────────────

func (r *redisImpl) Link(target, linkpath string) error {
	if err := r.rdb.Set(bg, linkpath, []byte(linkSentinel+target), 0).Err(); err != nil {
		return err
	}
	r.maintainIndex(linkpath, true)
	r.linkMu.Lock()
	r.links[linkpath] = linkEntry{checked: true, target: target}
	r.linkMu.Unlock()
	return nil
}

func (r *redisImpl) Unlink(linkpath string) error {
	if err := r.rdb.Del(bg, linkpath).Err(); err != nil {
		return err
	}
	r.maintainIndex(linkpath, false)
	r.linkMu.Lock()
	r.links[linkpath] = linkEntry{checked: true, target: ""} // 确认非链接
	r.linkMu.Unlock()
	return nil
}

// checkLink 返回 path 的链接目标；非链接或未知时返回 ""。
// 结果缓存在 linkEntry 中，已检查的路径不再访问 Redis。
func (r *redisImpl) checkLink(path string) string {
	r.linkMu.RLock()
	e := r.links[path] // 零值 {checked:false} 表示未检查
	r.linkMu.RUnlock()
	if e.checked {
		return e.target
	}
	// 未检查：向 Redis 查询
	var target string
	raw, _ := r.rdb.Get(bg, path).Bytes()
	if len(raw) >= 2 && raw[0] == '-' && raw[1] == '>' {
		target = string(raw[2:])
	}
	r.linkMu.Lock()
	r.links[path] = linkEntry{checked: true, target: target}
	r.linkMu.Unlock()
	return target
}

// ── CRUD ─────────────────────────────────────────────────────────────────────

func (r *redisImpl) Get(key string) (kvspace.XValue, error) {
	raw, err := r.rdb.Get(bg, kvspace.ResolveCore(key, r.checkLink)).Bytes()
	if err != nil {
		if errors.Is(err, goredis.Nil) {
			return kvspace.XValue{}, kvspace.ErrNotFound
		}
		return kvspace.XValue{}, err
	}
	return kvspace.DecodeXValue(raw), nil
}

func (r *redisImpl) GetMany(keys []string) ([]kvspace.XValue, error) {
	resolved := make([]string, len(keys))
	for i, k := range keys {
		resolved[i] = kvspace.ResolveCore(k, r.checkLink)
	}
	raw, err := r.rdb.MGet(bg, resolved...).Result()
	if err != nil {
		return nil, err
	}
	result := make([]kvspace.XValue, len(raw))
	for i, v := range raw {
		if v != nil {
			result[i] = kvspace.DecodeXValue([]byte(v.(string)))
		}
	}
	return result, nil
}

func (r *redisImpl) Set(key string, val kvspace.XValue) error {
	resolved := kvspace.ResolveCore(key, r.checkLink)
	r.maintainIndex(resolved, true)
	return r.rdb.Set(bg, resolved, kvspace.EncodeXValue(val), 0).Err()
}

// SetMany 使用 pipeline 批量写入，N 对 key 的索引维护合并为单次 round trip。
func (r *redisImpl) SetMany(pairs []kvspace.KVPair) error {
	if len(pairs) == 0 {
		return nil
	}
	pipe := r.rdb.Pipeline()
	msetArgs := make([]any, 0, len(pairs)*2)
	for _, p := range pairs {
		resolved := kvspace.ResolveCore(p.Key, r.checkLink)
		pipeIndex(pipe, resolved, true)
		msetArgs = append(msetArgs, resolved, kvspace.EncodeXValue(p.Val))
	}
	pipe.MSet(bg, msetArgs...)
	_, err := pipe.Exec(bg)
	return err
}

func (r *redisImpl) Del(keys ...string) error {
	resolved := make([]string, len(keys))
	for i, k := range keys {
		resolved[i] = kvspace.ResolveCore(k, r.checkLink)
		r.maintainIndex(resolved[i], false)
	}
	return r.rdb.Del(bg, resolved...).Err()
}

func (r *redisImpl) DelTree(prefix string) error {
	if r.checkLink(prefix) != "" {
		return r.Unlink(prefix) // 链接只删链接，不动 target 树
	}
	resolved := kvspace.ResolveCore(prefix, r.checkLink)
	r.delRecursive(resolved)
	r.maintainIndex(resolved, false)
	return nil
}

func (r *redisImpl) List(prefix string) ([]string, error) {
	return r.rdb.SMembers(bg, kvspace.ResolveCore(prefix, r.checkLink)+"/.").Result()
}

func (r *redisImpl) Watch(key string, timeout time.Duration) (kvspace.XValue, error) {
	vals, err := r.rdb.BLPop(bg, timeout, kvspace.ResolveCore(key, r.checkLink)).Result()
	if err != nil {
		if errors.Is(err, goredis.Nil) {
			return kvspace.XValue{}, kvspace.ErrNotFound
		}
		return kvspace.XValue{}, err
	}
	if len(vals) < 2 {
		return kvspace.XValue{}, kvspace.ErrNotFound
	}
	return kvspace.DecodeXValue([]byte(vals[1])), nil
}

func (r *redisImpl) Notify(key string, val kvspace.XValue) error {
	return r.rdb.LPush(bg, kvspace.ResolveCore(key, r.checkLink), kvspace.EncodeXValue(val)).Err()
}

func (r *redisImpl) DisConn() error { return r.rdb.Close() }

// ── 内部工具 ──────────────────────────────────────────────────────────────────

func (r *redisImpl) delRecursive(prefix string) {
	children, _ := r.rdb.SMembers(bg, prefix+"/.").Result()
	for _, c := range children {
		r.delRecursive(prefix + "/" + c)
	}
	r.rdb.Del(bg, prefix, prefix+"/.")
}

// maintainIndex 维护单条 key 的层级索引（每级一次 SADD/SREM）。
func (r *redisImpl) maintainIndex(key string, add bool) {
	prefix := ""
	for _, p := range strings.Split(key, "/")[1:] {
		if p == "" || p == "." {
			break
		}
		parent := prefix
		if parent == "" {
			parent = "/"
		}
		if add {
			r.rdb.SAdd(bg, parent+"/.", p)
		} else {
			r.rdb.SRem(bg, parent+"/.", p)
		}
		prefix += "/" + p
	}
}

// pipeIndex 向 pipeline 追加该 key 的全部层级 SADD/SREM 索引命令。
// 供 SetMany 使用，将多 key 的索引维护合并为一次 pipeline 执行。
func pipeIndex(pipe goredis.Pipeliner, key string, add bool) {
	prefix := ""
	for _, p := range strings.Split(key, "/")[1:] {
		if p == "" || p == "." {
			break
		}
		parent := prefix
		if parent == "" {
			parent = "/"
		}
		if add {
			pipe.SAdd(bg, parent+"/.", p)
		} else {
			pipe.SRem(bg, parent+"/.", p)
		}
		prefix += "/" + p
	}
}
