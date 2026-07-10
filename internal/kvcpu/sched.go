// Package sched 负责原子拾取和等待虚线程。
package kvcpu

import (
	"context"
	"encoding/json"
	"strings"
	"time"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/logx"
	"kvlang/internal/vthread"
)

// Pick 扫描 /vthread/ 子项，原子抢占 status=init 的 vthread。返回 vtid 或空串。
// kv.List 返回子项名（非完整路径），需用 keytree.VThread(vtid) 构造完整 key。
func Pick(ctx context.Context, kv kvspace.KVSpace) string {
	vtids, err := kv.List(keytree.VthreadRoot)
	if err != nil {
		logx.Debug("picker list error: %v", err)
		return ""
	}
	for _, vtid := range vtids {
		fullKey := keytree.VThread(vtid)
		val, err := kv.Get(fullKey)
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
		updated := vthread.VThread{PC: s.PC, Status: "running", Mode: s.Mode}
		data, _ := json.Marshal(updated)
		kv.Set(fullKey, data, 0)
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

