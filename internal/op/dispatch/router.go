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

// instInfo 是后端实例在 /sys/{op,heap}-plat/<inst> 中存储的注册信息。
type instInfo struct {
	Program string  `json:"program"` // 后端名，如 "op-cuda"
	Status  string  `json:"status"`  // "running" | "stopped"
	Load    float64 `json:"load"`    // 负载 [0,1]
}

// selectPlat 通用实例选择器：
//   - root    = "/sys/op-plat" 或 "/sys/heap-plat"
//   - prefix  = "op-" 或 "heap-"
//   - backend = 已确定的后端名，如 "cuda"
//
// 返回去掉前缀后的实例标识符，如 "cuda:0"。
func selectPlat(ctx context.Context, kv kvspace.KVSpace, root, prefix, backend string) (string, error) {
	instances, err := kv.List(root)
	if err != nil {
		return "", fmt.Errorf("list %s: %w", root, err)
	}

	bestLoad := math.MaxFloat64
	bestInstance := ""

	for _, inst := range instances {
		// inst = "op-cuda:0" 或 "heap-pytorch:1" 等
		nameWithoutPrefix := strings.TrimPrefix(inst, prefix)
		// nameWithoutPrefix = "cuda:0"，取冒号前得 "cuda"
		instBackend := nameWithoutPrefix
		if idx := strings.IndexByte(nameWithoutPrefix, ':'); idx >= 0 {
			instBackend = nameWithoutPrefix[:idx]
		}
		if instBackend != backend {
			continue
		}

		val, err := kv.Get(root + "/" + inst)
		if err != nil {
			continue
		}
		var info instInfo
		if err := json.Unmarshal([]byte(val), &info); err != nil {
			logx.Debug("selectPlat: unmarshal %s/%s: %v", root, inst, err)
			continue
		}
		if info.Status != "running" {
			continue
		}
		if info.Load < bestLoad {
			bestLoad = info.Load
			bestInstance = nameWithoutPrefix // "cuda:0"
		}
	}

	if bestInstance == "" {
		return "", fmt.Errorf("no running %s instance for backend=%s", root, backend)
	}
	return bestInstance, nil
}

// Select 根据 opcode 动态选择负载最低的 op-plat 实例。
// 返回实例标识符（去掉 "op-" 前缀），如 "cuda:0"、"pytorch:1"。
//
// opcode 支持带 vtype 前缀（如 "tensor.matmul"）；查找后端时自动剥离前缀，
// 后端仅需注册裸操作名（如 "matmul"）。
//
// 检测流程：
//  1. kv.List("/op")                    → 所有已注册后端名，如 ["cuda", "pytorch", "jax"]
//  2. /op/<backend>/func/<opname>       → 筛选支持该操作的后端（opname 已剥离前缀）
//  3. kv.List("/sys/op-plat")           → 运行中实例，如 ["op-cuda:0", "op-pytorch:0"]
//  4. 读取 {status, load}，选负载最低者
func Select(ctx context.Context, kv kvspace.KVSpace, opcode string) (string, error) {
	// 剥离 vtype 前缀："tensor.matmul" → "matmul"
	opname := stripVTypePrefix(opcode)

	backends, err := kv.List(keytree.OpRoot)
	if err != nil {
		return "", fmt.Errorf("list /op backends: %w", err)
	}

	var chosenBackend string
	for _, backend := range backends {
		if _, err := kv.Get(keytree.OpBackendFunc(backend, opname)); err == nil {
			chosenBackend = backend
			break
		}
	}
	if chosenBackend == "" {
		return "", fmt.Errorf("no registered backend supports opcode=%s", opcode)
	}

	return selectPlat(ctx, kv, keytree.SysOpPlatRoot, "op-", chosenBackend)
}

// SelectHeap 动态选择负载最低的 heap-plat 实例。
// heap-plat 负责张量生命周期（new/del/clone），不区分 opcode。
//
// 检测流程：
//  1. kv.List("/sys/heap-plat")  → 运行中实例，如 ["heap-cuda:0", "heap-cpu:0"]
//  2. 读取 {status, load}，选负载最低者（不限后端类型）
func SelectHeap(ctx context.Context, kv kvspace.KVSpace) (string, error) {
	instances, err := kv.List(keytree.SysHeapPlatRoot)
	if err != nil {
		return "", fmt.Errorf("list /sys/heap-plat: %w", err)
	}

	bestLoad := math.MaxFloat64
	bestInstance := ""

	for _, inst := range instances {
		val, err := kv.Get(keytree.SysHeapPlatInst(inst))
		if err != nil {
			continue
		}
		var info instInfo
		if err := json.Unmarshal([]byte(val), &info); err != nil {
			logx.Debug("SelectHeap: unmarshal %s: %v", inst, err)
			continue
		}
		if info.Status != "running" {
			continue
		}
		if info.Load < bestLoad {
			bestLoad = info.Load
			bestInstance = strings.TrimPrefix(inst, "heap-") // "cuda:0"
		}
	}

	if bestInstance == "" {
		return "", fmt.Errorf("no running heap-plat instance")
	}
	return bestInstance, nil
}

// ListBackends 返回当前 kvspace 中所有已注册后端名称。
// 即 kv.List("/op") 的结果，如 ["cuda", "pytorch", "jax", "triton", "tvm"]。
func ListBackends(ctx context.Context, kv kvspace.KVSpace) ([]string, error) {
	return kv.List(keytree.OpRoot)
}

// BackendSupports 返回 backend 是否支持某 opcode。
func BackendSupports(ctx context.Context, kv kvspace.KVSpace, backend, opcode string) bool {
	_, err := kv.Get(keytree.OpBackendFunc(backend, opcode))
	return err == nil
}

// stripVTypePrefix 剥离 vtype 命名空间前缀。
// "tensor.matmul" → "matmul"；无前缀则原样返回。
// dispatch 包不导入 vtype（避免循环），此为本地复制。
func stripVTypePrefix(opcode string) string {
	if dot := strings.IndexByte(opcode, '.'); dot > 0 {
		return opcode[dot+1:]
	}
	return opcode
}
