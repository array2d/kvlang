package dispatch

import (
	"context"
	"encoding/json"
	"fmt"
		"kvlang/internal/logx"
	"kvlang/internal/keytree"
	"math"
	"strings"

	"kvlang/internal/kvspace"
)

// Select 根据 opcode 选择负载最低的 op-plat 实例
// 返回实例标识符, e.g., "metal:0", "cuda:1"
func Select(ctx context.Context, kv kvspace.KVSpace, opcode string) (string, error) {
	// 1. 找到支持该算子的所有程序
	programs, err := kv.List(keytree.OpRoot)
	if err != nil {
		return "", fmt.Errorf("list op programs: %w", err)
	}

	var chosenProgram string
	for _, progKey := range programs {
		parts := strings.Split(progKey, "/")
		if len(parts) < 3 {
			continue
		}
		backend := parts[2]
		if _, err := kv.Get(keytree.OpBackendFunc(backend, opcode)); err == nil {
			chosenProgram = backend
			break
		}
	}

	if chosenProgram == "" {
		return "", fmt.Errorf("no op-plat supports opcode: %s", opcode)
	}

	// 2. 选择该程序下负载最低的进程实例
	instances, err := kv.List(keytree.SysRoot + "/op-plat")
	if err != nil {
		return "", fmt.Errorf("list op-plat instances: %w", err)
	}

	type instInfo struct {
		Program string  `json:"program"`
		Status  string  `json:"status"`
		Load    float64 `json:"load"`
	}

	bestLoad := math.MaxFloat64
	bestInstance := ""

	for _, instKey := range instances {
		if !strings.Contains(instKey, chosenProgram) {
			continue
		}

		val, err := kv.Get(instKey)
		if err != nil {
			continue
		}
		var info instInfo
		if err := json.Unmarshal([]byte(val), &info); err != nil {
			logx.Debug("route.Select: unmarshal instance info %s: %v", instKey, err)
			continue
		}

		if info.Status != "running" {
			continue
		}
		if info.Load < bestLoad {
			bestLoad = info.Load
			// "/sys/op-plat/op-metal:0" → "metal:0"
			parts := strings.Split(instKey, "/")
			lastPart := parts[len(parts)-1]
			bestInstance = strings.TrimPrefix(lastPart, "op-")
		}
	}

	if bestInstance == "" {
		return "", fmt.Errorf("no running op-plat instance for %s (program %s)", opcode, chosenProgram)
	}

	return bestInstance, nil
}

// DetermineBackend 判断 func 的编译后端 (按优先级)
func DetermineBackend(ctx context.Context, kv kvspace.KVSpace, funcName string) string {
	for _, b := range []string{"op-metal", "op-cuda", "op-cpu"} {
		if _, err := kv.Get(keytree.OpBackendFunc(b, funcName)); err == nil {
			return b
		}
	}
	return "op-metal"
}
