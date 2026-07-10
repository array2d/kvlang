package kvspace

import (
	"context"
	"time"
	"strings"

	"github.com/redis/go-redis/v9"
)

var bg = context.Background()

// Conn 根据 DSN 创建 KVSpace。
func Conn(dsn string) KVSpace {
	return &redisImpl{
		rdb: redis.NewClient(&redis.Options{
			Addr:         dsn,
			PoolSize:     8,
			MinIdleConns: 4,
			PoolTimeout:  10 * time.Second,
			ReadTimeout:  3 * time.Second,
			WriteTimeout: 3 * time.Second,
		}),
	}
}

type redisImpl struct {
	rdb *redis.Client
}

func (r *redisImpl) Get(key string) (string, error)          { return r.rdb.Get(bg, key).Result() }
func (r *redisImpl) Set(key string, value any, ttl time.Duration) error {
	// 自动维护目录索引: /a/b/c → SADD /a/. "b", SADD /a/b/. "c"
	r.maintainIndex(key, true)
	return r.rdb.Set(bg, key, value, ttl).Err()
}
func (r *redisImpl) Del(keys ...string) error {
	for _, k := range keys {
		r.maintainIndex(k, false)
	}
	return r.rdb.Del(bg, keys...).Err()
}
func (r *redisImpl) MGet(keys ...string) ([]any, error) { return r.rdb.MGet(bg, keys...).Result() }
func (r *redisImpl) List(prefix string) ([]string, error) {
	return r.rdb.SMembers(bg, prefix+"/.").Result()
}
func (r *redisImpl) Watch(timeout time.Duration, keys ...string) ([]string, error) {
	return r.rdb.BLPop(bg, timeout, keys...).Result()
}
func (r *redisImpl) Notify(key string, values ...any) error {
	return r.rdb.LPush(bg, key, values...).Err()
}
func (r *redisImpl) DisConn() error { return r.rdb.Close() }

// maintainIndex 维护目录索引: /a/b/c → /.{a}, /a/.{b}, /a/b/.{c}
func (r *redisImpl) maintainIndex(key string, add bool) {
	for i := 0; i < len(key); i++ {
		if key[i] != '/' { continue }
		parent := key[:i]
		if parent == "" { parent = "/" }
		rest := key[i+1:]
		if slash := strings.IndexByte(rest, '/'); slash >= 0 {
			rest = rest[:slash]
		}
		if rest == "" || rest == "." { continue }
		if add {
			r.rdb.SAdd(bg, parent+"/.", rest)
		} else {
			r.rdb.SRem(bg, parent+"/.", rest)
		}
	}
}

