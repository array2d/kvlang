// Package vthread 管理 vthread 状态管理与 kvspace 持久化。
package vthread

import (
	"context"
	"encoding/json"
	"fmt"
	"kvlang/internal/keytree"
	"kvlang/internal/logx"
	"time"

	"kvlang/internal/kvspace"
)

// VThread 存储在 /vthread/<vtid> 中，表示运行时状态。
type VThread struct {
	PC     string            `json:"pc"`
	Status string            `json:"status"`
	Mode   string            `json:"mode,omitempty"` // "single" | "batch", 默认 "single"
	Error  map[string]string `json:"error,omitempty"`
}

// Get 读取 vthread 当前状态。
func Get(ctx context.Context, kv kvspace.KVSpace, vtid string) VThread {
	val, err := kv.Get(keytree.VThread(vtid))
	if err != nil {
		return VThread{Status: "error"}
	}
	var s VThread
	if err := json.Unmarshal([]byte(val), &s); err != nil {
		logx.Warn("state.Get: unmarshal vthread %s: %v", vtid, err)
		return VThread{Status: "error"}
	}
	return s
}

// Set 更新 vthread 的 PC 和 status，保留 Mode 等其他字段。
func Set(ctx context.Context, kv kvspace.KVSpace, vtid string, pc, status string) {
	cur := Get(ctx, kv, vtid)
	cur.PC = pc
	cur.Status = status
	cur.Error = nil // 清除上一次错误（Set 表示正常推进）
	data, err := json.Marshal(cur)
	if err != nil {
		logx.Warn("state.Set: marshal vthread %s: %v", vtid, err)
		return
	}
	kv.Set(keytree.VThread(vtid), data)
}

// SetError 标记 vthread 为 error 状态。
func SetError(ctx context.Context, kv kvspace.KVSpace, vtid string, pc string, errMsg string) {
	s := map[string]interface{}{
		"pc":     pc,
		"status": "error",
		"error":  map[string]string{"code": "VM_ERROR", "message": errMsg},
	}
	data, err := json.Marshal(s)
	if err != nil {
		logx.Warn("state.SetError: marshal vthread %s: %v", vtid, err)
		return
	}
	kv.Set(keytree.VThread(vtid), data)
}

// CreateVThread 在 kvspace 中创建一个新虚线程。
func CreateVThread(ctx context.Context, kv kvspace.KVSpace, funcName string, reads, writes []string) (string, error) {
	vtid := fmt.Sprintf("test-%d", time.Now().UnixNano())
	st := VThread{PC: "[0,0]", Status: "init", Mode: "single"}
	data, _ := json.Marshal(st)
	if err := kv.Set(keytree.VThread(vtid), data); err != nil {
		return "", fmt.Errorf("set state: %w", err)
	}
	kv.Set(keytree.VThreadSlot(vtid, 0, 0), funcName)
	for i, r := range reads {
		kv.Set(keytree.VThreadSlot(vtid, 0, -(i+1)), r)
	}
	for i, w := range writes {
		kv.Set(keytree.VThreadSlot(vtid, 0, i+1), w)
	}

	return vtid, nil
}

// WaitDone 阻塞等待 op-plat / heap-plat 完成通知。
func WaitDone(ctx context.Context, kv kvspace.KVSpace, vtid string, timeout time.Duration) (map[string]interface{}, error) {
	doneKey := keytree.Done(vtid)
	result, err := kv.Watch(doneKey, timeout)
	if err != nil {
		return nil, fmt.Errorf("waitDone timeout for %s: %w", doneKey, err)
	}
	var done map[string]interface{}
	if result != "" {
		if err := json.Unmarshal([]byte(result), &done); err != nil {
			logx.Warn("state.WaitDone: unmarshal done result for %s: %v", vtid, err)
			return nil, fmt.Errorf("unmarshal done result: %w", err)
		}
	}
	return done, nil
}
