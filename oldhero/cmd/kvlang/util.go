package main

import (
	"os"

	"github.com/array2d/kvspace-go"
)

// initDirs 创建基础目录 /lib/ 与 /vthread/（layout/run 前必须存在）。
func initDirs(kv kvspace.KVSpace) {
	kvspace.MkIndexRecursive(kv, "/lib/")
	kvspace.MkIndexRecursive(kv, "/vthread/")
}

// defaultKVSpace 返回 kvspace DSN 默认值：KVSPACE 环境变量覆盖，否则本机 redis。
func defaultKVSpace() string {
	if v := os.Getenv("KVSPACE"); v != "" {
		return v
	}
	return "redis://127.0.0.1:6379"
}

const kvspaceFlagDesc = "kvspace 地址（DSN，如 redis://host:port、art:// 进程内 ART；默认可由 KVSPACE 覆盖）"


