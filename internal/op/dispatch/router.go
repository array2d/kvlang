package dispatch

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"strings"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/logx"
)

// instInfo 是后端实例在 /sys/op/<backend>/<n> 中存储的注册信息。
type instInfo struct {
	Status string  `json:"status"` // "running" | "stopped"
	Load   float64 `json:"load"`   // 负载 [0,1]
}

// Select 根据 opcode 选择负载最低的 op 实例。
// 返回 (backend, n, error)，调用方用 keytree.SysOpCmd(backend, n) 构造命令队列。
//
// 流程：
//  1. kv.List("/sys/op")               → 所有已注册 backend 名，如 ["buildin","cuda"]
//  2. /sys/op/<backend>/func/<opname>  → 筛选支持该操作的 backend
//  3. kv.List("/sys/op/<backend>")     → 子项，过滤掉 "func"，剩下实例编号 ["0","1",…]
//  4. 读取各实例 {status, load}，选负载最低的 running 实例
func Select(ctx context.Context, kv kvspace.KVSpace, opcode string) (backend, n string, err error) {
	opname := stripVTypePrefix(opcode)

	backends, err := kv.List(keytree.SysOpRoot)
	if err != nil {
		return "", "", fmt.Errorf("list %s: %w", keytree.SysOpRoot, err)
	}

	for _, b := range backends {
		if _, err := kv.Get(keytree.SysOpFunc(b, opname)); err == nil {
			backend = b
			break
		}
	}
	if backend == "" {
		return "", "", fmt.Errorf("no backend supports opcode=%s", opcode)
	}

	children, err := kv.List(keytree.SysOpRoot + "/" + backend)
	if err != nil {
		return "", "", fmt.Errorf("list backend %s instances: %w", backend, err)
	}

	bestLoad := math.MaxFloat64
	for _, child := range children {
		if child == "func" {
			continue // 跳过 /sys/op/<backend>/func/ 子树
		}
		val, err := kv.Get(keytree.SysOp(backend, child))
		if err != nil {
			continue
		}
		var info instInfo
		if json.Unmarshal([]byte(val.Str()), &info) != nil {
			logx.Debug("Select: unmarshal %s/%s: invalid", backend, child)
			continue
		}
		if info.Status != "running" {
			continue
		}
		if info.Load < bestLoad {
			bestLoad = info.Load
			n = child
		}
	}

	if n == "" {
		return "", "", fmt.Errorf("no running instance for backend=%s", backend)
	}
	return backend, n, nil
}

// ListBackends 返回所有已注册 backend 名称（kv.List("/sys/op") 结果）。
func ListBackends(ctx context.Context, kv kvspace.KVSpace) ([]string, error) {
	return kv.List(keytree.SysOpRoot)
}

// BackendSupports 返回 backend 是否支持某 opcode。
func BackendSupports(ctx context.Context, kv kvspace.KVSpace, backend, opcode string) bool {
	_, err := kv.Get(keytree.SysOpFunc(backend, stripVTypePrefix(opcode)))
	return err == nil
}

// stripVTypePrefix 剥离 vtype 命名空间前缀。
// "tensor.matmul" → "matmul"；无前缀则原样返回。
func stripVTypePrefix(opcode string) string {
	if dot := strings.IndexByte(opcode, '.'); dot > 0 {
		return opcode[dot+1:]
	}
	return opcode
}
