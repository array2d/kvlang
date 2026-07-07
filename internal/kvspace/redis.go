package kvspace

import (
	"context"
	"time"

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
func (r *redisImpl) Set(key string, value any, ttl time.Duration) error { return r.rdb.Set(bg, key, value, ttl).Err() }
func (r *redisImpl) Del(keys ...string) error                 { return r.rdb.Del(bg, keys...).Err() }
func (r *redisImpl) MGet(keys ...string) ([]any, error)       { return r.rdb.MGet(bg, keys...).Result() }
func (r *redisImpl) Keys(pattern string) ([]string, error)    { return r.rdb.Keys(bg, pattern).Result() }
func (r *redisImpl) Incr(key string) (int64, error)           { return r.rdb.Incr(bg, key).Result() }
func (r *redisImpl) RPush(key string, values ...any) error    { return r.rdb.RPush(bg, key, values...).Err() }
func (r *redisImpl) LRange(key string, start, stop int64) ([]string, error) {
	return r.rdb.LRange(bg, key, start, stop).Result()
}
func (r *redisImpl) Watch(timeout time.Duration, keys ...string) ([]string, error) {
	return r.rdb.BLPop(bg, timeout, keys...).Result()
}
func (r *redisImpl) Notify(key string, values ...any) error {
	return r.rdb.LPush(bg, key, values...).Err()
}
func (r *redisImpl) HMGet(key string, fields ...string) ([]any, error) {
	return r.rdb.HMGet(bg, key, fields...).Result()
}
func (r *redisImpl) Eval(script string, keys []string, args ...any) (int64, error) {
	return r.rdb.Eval(bg, script, keys, args...).Int64()
}
func (r *redisImpl) Pipeline() Pipeliner { return &redisPipe{pipe: r.rdb.Pipeline()} }
func (r *redisImpl) DisConn() error { return r.rdb.Close() }

type redisPipe struct {
	pipe redis.Pipeliner
}

func (p *redisPipe) Set(key string, value any, ttl time.Duration) { p.pipe.Set(bg, key, value, ttl) }
func (p *redisPipe) Exec() ([]any, error) {
	cmders, err := p.pipe.Exec(bg)
	if err != nil {
		return nil, err
	}
	result := make([]any, len(cmders))
	for i, c := range cmders {
		result[i] = c
	}
	return result, nil
}
