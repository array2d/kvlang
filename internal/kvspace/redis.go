package kvspace

import (
	"context"
	"errors"
	"strings"
	"sync"
	"time"

	"github.com/redis/go-redis/v9"
)

var bg = context.Background()

// value 以 "->" 开头表示软链接（类比 ls: "name -> target"）。
const linkSentinel = "->"

func Conn(dsn string) KVSpace { return ConnPool(dsn, 16) }

// ConnPool 创建 KVSpace。serve 模式下 poolSize 建议设为 workers+16。
func ConnPool(dsn string, poolSize int) KVSpace {
	if poolSize < 16 {
		poolSize = 16
	}
	return &redisImpl{
		rdb: redis.NewClient(&redis.Options{
			Addr:         dsn,
			PoolSize:     poolSize,
			MinIdleConns: min(poolSize/4, 8),
			PoolTimeout:  10 * time.Second,
			ReadTimeout:  3 * time.Second,
			WriteTimeout: 3 * time.Second,
		}),
		links: make(map[string]string),
	}
}

type redisImpl struct {
	rdb    *redis.Client
	linkMu sync.RWMutex
	// links: 非空 = 链接 target；"" = 否定缓存（确认非链接）；不存在 = 未检查（lazy GET）
	links map[string]string
}

// ── 软链接 ───────────────────────────────────────────────────────────────────

func (r *redisImpl) Link(target, linkpath string) error {
	if err := r.rdb.Set(bg, linkpath, []byte(linkSentinel+target), 0).Err(); err != nil {
		return err
	}
	r.maintainIndex(linkpath, true)
	r.linkMu.Lock()
	r.links[linkpath] = target
	r.linkMu.Unlock()
	return nil
}

func (r *redisImpl) Unlink(linkpath string) error {
	if err := r.rdb.Del(bg, linkpath).Err(); err != nil {
		return err
	}
	r.maintainIndex(linkpath, false)
	r.linkMu.Lock()
	r.links[linkpath] = "" // 否定缓存
	r.linkMu.Unlock()
	return nil
}

// checkLink 返回 path 的链接 target；非链接返回 ""。
func (r *redisImpl) checkLink(path string) string {
	r.linkMu.RLock()
	t, known := r.links[path]
	r.linkMu.RUnlock()
	if known {
		return t
	}
	raw, _ := r.rdb.Get(bg, path).Bytes()
	if len(raw) >= 2 && raw[0] == '-' && raw[1] == '>' {
		t = string(raw[2:])
	}
	r.linkMu.Lock()
	r.links[path] = t
	r.linkMu.Unlock()
	return t
}

// resolveCore 路径解析核心：逐 '/' 边界从短到长查链接，上限 40 跳防环。
func resolveCore(path string, lookup func(string) string) string {
	for range 40 {
		found := false
		for i := 1; i < len(path); i++ {
			if path[i] != '/' {
				continue
			}
			if t := lookup(path[:i]); t != "" {
				path, found = t+path[i:], true
				break
			}
		}
		if !found {
			if t := lookup(path); t != "" {
				path, found = t, true
			}
		}
		if !found {
			return path
		}
	}
	return path
}

// ── CRUD ─────────────────────────────────────────────────────────────────────

func (r *redisImpl) Get(key string) (Value, error) {
	raw, err := r.rdb.Get(bg, resolveCore(key, r.checkLink)).Bytes()
	if err != nil {
		if errors.Is(err, redis.Nil) {
			return Value{}, ErrNotFound
		}
		return Value{}, err
	}
	return DecodeValue(raw), nil
}

func (r *redisImpl) GetMany(keys []string) ([]Value, error) {
	resolved := make([]string, len(keys))
	for i, k := range keys {
		resolved[i] = resolveCore(k, r.checkLink)
	}
	raw, err := r.rdb.MGet(bg, resolved...).Result()
	if err != nil {
		return nil, err
	}
	result := make([]Value, len(raw))
	for i, v := range raw {
		if v != nil {
			result[i] = DecodeValue([]byte(v.(string)))
		}
	}
	return result, nil
}

func (r *redisImpl) Set(key string, val Value) error {
	resolved := resolveCore(key, r.checkLink)
	r.maintainIndex(resolved, true)
	return r.rdb.Set(bg, resolved, EncodeValue(val), 0).Err()
}

func (r *redisImpl) SetMany(pairs []KVPair) error {
	if len(pairs) == 0 {
		return nil
	}
	args := make([]any, 0, len(pairs)*2)
	for _, p := range pairs {
		resolved := resolveCore(p.Key, r.checkLink)
		r.maintainIndex(resolved, true)
		args = append(args, resolved, EncodeValue(p.Val))
	}
	return r.rdb.MSet(bg, args...).Err()
}

func (r *redisImpl) Del(keys ...string) error {
	resolved := make([]string, len(keys))
	for i, k := range keys {
		resolved[i] = resolveCore(k, r.checkLink)
		r.maintainIndex(resolved[i], false)
	}
	return r.rdb.Del(bg, resolved...).Err()
}

func (r *redisImpl) DelTree(prefix string) error {
	if r.checkLink(prefix) != "" {
		return r.Unlink(prefix) // 链接只删链接，不动 target 树
	}
	resolved := resolveCore(prefix, r.checkLink)
	r.delRecursive(resolved)
	r.maintainIndex(resolved, false)
	return nil
}

func (r *redisImpl) List(prefix string) ([]string, error) {
	return r.rdb.SMembers(bg, resolveCore(prefix, r.checkLink)+"/.").Result()
}

func (r *redisImpl) Watch(key string, timeout time.Duration) (Value, error) {
	vals, err := r.rdb.BLPop(bg, timeout, resolveCore(key, r.checkLink)).Result()
	if err != nil {
		if errors.Is(err, redis.Nil) {
			return Value{}, ErrNotFound
		}
		return Value{}, err
	}
	if len(vals) < 2 {
		return Value{}, ErrNotFound
	}
	return DecodeValue([]byte(vals[1])), nil
}

func (r *redisImpl) Notify(key string, val Value) error {
	return r.rdb.LPush(bg, resolveCore(key, r.checkLink), EncodeValue(val)).Err()
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
