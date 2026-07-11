package kvspace

import (
	"context"
	"fmt"
	"strings"
	"time"

	"github.com/redis/go-redis/v9"
)

var bg = context.Background()

// Conn 根据 DSN 创建 KVSpace（默认连接池大小 16）。
func Conn(dsn string) KVSpace {
	return ConnPool(dsn, 16)
}

// ConnPool 根据 DSN 创建 KVSpace，使用指定连接池大小。
// serve 模式下 worker 数量较多，需要更大的连接池（workers+16）。
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
	}
}

type redisImpl struct {
	rdb *redis.Client
}

// Get 读取单个 key。key 不存在时返回 ("", redis.Nil)。
func (r *redisImpl) Get(key string) (string, error) {
	return r.rdb.Get(bg, key).Result()
}

// Gets 批量读取，使用 MGET。缺失 key 对应位置返回 ""。
func (r *redisImpl) Gets(keys ...string) ([]string, error) {
	raw, err := r.rdb.MGet(bg, keys...).Result()
	if err != nil {
		return nil, err
	}
	result := make([]string, len(raw))
	for i, v := range raw {
		if v != nil {
			result[i] = fmt.Sprint(v)
		}
	}
	return result, nil
}

// Set 写入单个 key（自动维护目录索引）。
func (r *redisImpl) Set(key string, value any) error {
	r.maintainIndex(key, true)
	return r.rdb.Set(bg, key, value, 0).Err()
}

// Sets 批量写入，使用 Redis MSET（维护每个 key 的目录索引）。
func (r *redisImpl) Sets(kvs map[string]any) error {
	if len(kvs) == 0 {
		return nil
	}
	pairs := make([]any, 0, len(kvs)*2)
	for k, v := range kvs {
		r.maintainIndex(k, true)
		pairs = append(pairs, k, v)
	}
	return r.rdb.MSet(bg, pairs...).Err()
}

// Del 删除指定 key，并更新目录索引。
func (r *redisImpl) Del(keys ...string) error {
	for _, k := range keys {
		r.maintainIndex(k, false)
	}
	return r.rdb.Del(bg, keys...).Err()
}

// DelR 递归删除 prefix 及其所有子项（含目录索引），并从父目录中移除。
func (r *redisImpl) DelR(prefix string) error {
	r.delRecursive(prefix)
	r.maintainIndex(prefix, false)
	return nil
}

// delRecursive 递归删除 prefix 下所有 key 和索引，不修改父目录索引。
func (r *redisImpl) delRecursive(prefix string) {
	children, _ := r.rdb.SMembers(bg, prefix+"/.").Result()
	for _, c := range children {
		r.delRecursive(prefix + "/" + c)
	}
	r.rdb.Del(bg, prefix, prefix+"/.")
}

// List 列出 prefix 直接子项名（SMEMBERS prefix/.）。
func (r *redisImpl) List(prefix string) ([]string, error) {
	return r.rdb.SMembers(bg, prefix+"/.").Result()
}

// Watch 阻塞等待单 key 消息（BLPOP），返回消息值。
// timeout=0 永久阻塞；超时返回 ("", redis.Nil)。
func (r *redisImpl) Watch(key string, timeout time.Duration) (string, error) {
	vals, err := r.rdb.BLPop(bg, timeout, key).Result()
	if err != nil {
		return "", err
	}
	if len(vals) < 2 {
		return "", nil
	}
	return vals[1], nil
}

// Notify 向 key 推送一条消息（LPUSH）。
func (r *redisImpl) Notify(key string, value any) error {
	return r.rdb.LPush(bg, key, value).Err()
}

func (r *redisImpl) DisConn() error { return r.rdb.Close() }

// maintainIndex 维护目录索引: /a/b/c → SADD /a/. "b", SADD /a/b/. "c"
func (r *redisImpl) maintainIndex(key string, add bool) {
	for i := 0; i < len(key); i++ {
		if key[i] != '/' {
			continue
		}
		parent := key[:i]
		if parent == "" {
			parent = "/"
		}
		rest := key[i+1:]
		if slash := strings.IndexByte(rest, '/'); slash >= 0 {
			rest = rest[:slash]
		}
		if rest == "" || rest == "." {
			continue
		}
		if add {
			r.rdb.SAdd(bg, parent+"/.", rest)
		} else {
			r.rdb.SRem(bg, parent+"/.", rest)
		}
	}
}
