package main

import (
	"fmt"
	"os"
	"strconv"

	"kvlang/internal/keytree"
	"github.com/array2d/kvspace-go"
)

// defaultKVSpace 返回 kvspace DSN 默认值：KVLANG_KVSPACE 环境变量覆盖，否则本机 redis。
func defaultKVSpace() string {
	if v := os.Getenv("KVLANG_KVSPACE"); v != "" {
		return v
	}
	return "redis://127.0.0.1:6379"
}

const kvspaceFlagDesc = "kvspace 地址（DSN，如 redis://host:port；裸 host:port 视为 redis；默认可由 KVLANG_KVSPACE 覆盖）"

// incrVtid 原子自增 /vthread/seq 并返回新 vtid。
func incrVtid(kv kvspace.KVSpace) string {
	valV, _ := kv.Get(keytree.VthreadSeq)
	n, _ := strconv.ParseInt(valV.Str(), 10, 64)
	n++
	kv.Set(keytree.VthreadSeq, kvspace.Str(strconv.FormatInt(n, 10)))
	return fmt.Sprintf("%d", n)
}

