// Package sched 负责原子拾取和等待虚线程。
package kvcpu

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"kvlang/internal/logx"
	"kvlang/internal/vthread"
	"kvlang/internal/keytree"
	"strings"
	"kvlang/internal/kvspace"
)

// Pick 扫描 /vthread/*，原子抢占 status=init 的 vthread。返回 vtid 或空串。
func Pick(ctx context.Context, kv kvspace.KVSpace) string {
	keys, err := kv.Keys(keytree.VThreadPattern())
	if err != nil {
		logx.Debug("picker KEYS error: %v", err)
		return ""
	}
	for _, key := range keys {
		vtid := key[len(keytree.VThread("")):]
		// Skip non-numeric vtid (e.g., system keys nested under /vthread/)
		// 实际上 key pattern `/vthread/*` 不会匹配 `/vthread/1/sub`，但还是做一次 GET 检查
		val, err := kv.Get(key)
		if err != nil {
			continue
		}
		var s vthread.VThread
		if json.Unmarshal([]byte(val), &s) != nil {
			continue
		}
		if s.Status != "init" {
			continue
		}
		// 原子 CAS: set status to "running" if it's still "init"
		updated := vthread.VThread{PC: s.PC, Status: "running", Mode: s.Mode}
		data, _ := json.Marshal(updated)
		// Lua script for atomic compare-and-set
		script := `
			local key = KEYS[1]
			local val = redis.call('GET', key)
			if not val then return 0 end
			local decoded = cjson.decode(val)
			if decoded.status ~= 'init' then return 0 end
			redis.call('SET', key, ARGV[1])
			return 1
		`
		result, err := kv.Eval(script, []string{key}, string(data))
		if err != nil || result == 0 {
			continue
		}
		return vtid
	}
	return ""
}

// Wait 阻塞等待新的 vthread 通知 (BLPOP keytree.NotifyVM)。
func Wait(ctx context.Context, kv kvspace.KVSpace) {
	vals, err := kv.Watch(5*time.Second, keytree.NotifyVM)
	if err != nil {
		if !strings.Contains(err.Error(), "nil") {
			logx.Debug("picker BLPOP: %v", err)
		}
		return
	}
	if len(vals) >= 2 {
		var notify struct {
			Event string `json:"event"`
			Vtid  string `json:"vtid"`
		}
		if json.Unmarshal([]byte(vals[1]), &notify) == nil {
			logx.Debug("notify: %s vtid=%s", notify.Event, notify.Vtid)
		}
	}
}

var _ = fmt.Println // keep fmt import
