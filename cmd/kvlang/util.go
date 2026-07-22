package main

import (
	"os"
)

// defaultKVSpace 返回 kvspace DSN 默认值：KVLANG_KVSPACE 环境变量覆盖，否则本机 redis。
func defaultKVSpace() string {
	if v := os.Getenv("KVLANG_KVSPACE"); v != "" {
		return v
	}
	return "redis://127.0.0.1:6379"
}

const kvspaceFlagDesc = "kvspace 地址（DSN，如 redis://host:port；裸 host:port 视为 redis；默认可由 KVLANG_KVSPACE 覆盖）"


