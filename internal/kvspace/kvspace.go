// Package kvspace 抽象 KV 存储，当前实现为 Redis。
package kvspace

import (
	"context"
	"time"

	"github.com/redis/go-redis/v9"
)

// KVSpace KV 存储接口。
type KVSpace interface {
	Get(ctx context.Context, key string) (string, error)
	Set(ctx context.Context, key string, value any, ttl time.Duration) error
	Del(ctx context.Context, keys ...string) error
	Exists(ctx context.Context, keys ...string) (int64, error)
	MGet(ctx context.Context, keys ...string) ([]any, error)
	Keys(ctx context.Context, pattern string) ([]string, error)
	Incr(ctx context.Context, key string) (int64, error)
	RPush(ctx context.Context, key string, values ...any) error
	LRange(ctx context.Context, key string, start, stop int64) ([]string, error)
	LPop(ctx context.Context, key string) (string, error)
	BLPop(ctx context.Context, timeout time.Duration, keys ...string) ([]string, error)
	LPush(ctx context.Context, key string, values ...any) error
	HMGet(ctx context.Context, key string, fields ...string) ([]any, error)
	Eval(ctx context.Context, script string, keys []string, args ...any) (int64, error)
	FlushDB(ctx context.Context) error
	Ping(ctx context.Context) error
	Close() error
	Pipeline() Pipeliner
}

// Pipeliner 管道接口。
type Pipeliner interface {
	Set(ctx context.Context, key string, value any, ttl time.Duration)
	Exec(ctx context.Context) ([]redis.Cmder, error)
}

// NewRedis 创建 Redis 实现的 KVSpace。
func New(addr string) KVSpace {
	return &redisSpace{
		rdb: redis.NewClient(&redis.Options{
			Addr:         addr,
			PoolSize:     4,
			MinIdleConns: 1,
		}),
	}
}

// NewRedisWithPool 创建带连接池配置的 Redis KVSpace。
func NewWithPool(addr string, poolSize int) KVSpace {
	return &redisSpace{
		rdb: redis.NewClient(&redis.Options{
			Addr:         addr,
			PoolSize:     poolSize * 2,
			MinIdleConns: poolSize,
			PoolTimeout:  10 * time.Second,
			ReadTimeout:  3 * time.Second,
			WriteTimeout: 3 * time.Second,
		}),
	}
}

type redisSpace struct {
	rdb *redis.Client
}

func (r *redisSpace) Get(ctx context.Context, key string) (string, error) {
	return r.rdb.Get(ctx, key).Result()
}
func (r *redisSpace) Set(ctx context.Context, key string, value any, ttl time.Duration) error {
	return r.rdb.Set(ctx, key, value, ttl).Err()
}
func (r *redisSpace) Del(ctx context.Context, keys ...string) error {
	return r.rdb.Del(ctx, keys...).Err()
}
func (r *redisSpace) Exists(ctx context.Context, keys ...string) (int64, error) {
	return r.rdb.Exists(ctx, keys...).Result()
}
func (r *redisSpace) MGet(ctx context.Context, keys ...string) ([]any, error) {
	return r.rdb.MGet(ctx, keys...).Result()
}
func (r *redisSpace) Keys(ctx context.Context, pattern string) ([]string, error) {
	return r.rdb.Keys(ctx, pattern).Result()
}
func (r *redisSpace) Incr(ctx context.Context, key string) (int64, error) {
	return r.rdb.Incr(ctx, key).Result()
}
func (r *redisSpace) RPush(ctx context.Context, key string, values ...any) error {
	return r.rdb.RPush(ctx, key, values...).Err()
}
func (r *redisSpace) LRange(ctx context.Context, key string, start, stop int64) ([]string, error) {
	return r.rdb.LRange(ctx, key, start, stop).Result()
}
func (r *redisSpace) LPop(ctx context.Context, key string) (string, error) {
	return r.rdb.LPop(ctx, key).Result()
}
func (r *redisSpace) BLPop(ctx context.Context, timeout time.Duration, keys ...string) ([]string, error) {
	return r.rdb.BLPop(ctx, timeout, keys...).Result()
}
func (r *redisSpace) LPush(ctx context.Context, key string, values ...any) error {
	return r.rdb.LPush(ctx, key, values...).Err()
}
func (r *redisSpace) HMGet(ctx context.Context, key string, fields ...string) ([]any, error) {
	return r.rdb.HMGet(ctx, key, fields...).Result()
}
func (r *redisSpace) Eval(ctx context.Context, script string, keys []string, args ...any) (int64, error) {
	return r.rdb.Eval(ctx, script, keys, args...).Int64()
}
func (r *redisSpace) FlushDB(ctx context.Context) error {
	return r.rdb.FlushDB(ctx).Err()
}
func (r *redisSpace) Ping(ctx context.Context) error {
	return r.rdb.Ping(ctx).Err()
}
func (r *redisSpace) Close() error {
	return r.rdb.Close()
}
func (r *redisSpace) Pipeline() Pipeliner {
	return &redisPipe{pipe: r.rdb.Pipeline()}
}

type redisPipe struct {
	pipe redis.Pipeliner
}

func (p *redisPipe) Set(ctx context.Context, key string, value any, ttl time.Duration) {
	p.pipe.Set(ctx, key, value, ttl)
}
func (p *redisPipe) Exec(ctx context.Context) ([]redis.Cmder, error) {
	return p.pipe.Exec(ctx)
}
