package builtin

import (
	"context"

	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
)

// nativeOps VM 原生求值的算子集合（各 op 文件 init() 自行注册）。
var nativeOps = map[string]bool{}

// nativeSigs 内置算子签名（各 op 文件 init() 自行注册）。
// 格式: "rwir op(reads...) -> (writes...)"
// 类型: num(int64/float64), int64, float64, bool, string, any
var nativeSigs = map[string]string{}

// registry opcode → Op 实现（各 op 文件 init() 自行注册）。
var registry = map[string]Op{}

var bg = context.Background()

// Register 注册一个 native opcode：签名 + 实现。
// impl 为 nil 表示纯控制流算子（call/return/br/goto 不在 nativeOps 中）。
func Register(opcode, sig string, impl Op) {
	nativeOps[opcode] = true
	if sig != "" {
		nativeSigs[opcode] = sig
	}
	if impl != nil {
		registry[opcode] = impl
	}
}

// WriteSysRwir 将所有已注册的 native rwir 签名写入 /sys/rwir/。
// 基础类型（+, ==, print 等）→ /sys/rwir/opcode
// string.* 类型（string.len, string.char 等）→ /sys/rwir/string.opcode
// 应在 runtime 启动时调用（非 parser 阶段）。
func WriteSysRwir(kv kvspace.KVSpace) {
	pairs := make([]kvspace.KVPair, 0, len(nativeSigs))
	for opcode, sig := range nativeSigs {
		pairs = append(pairs, kvspace.KVPair{
			Key: keytree.SysRwir(opcode),
			Val: kvspace.Raw("rwir", []byte(sig)),
		})
	}
	if len(pairs) > 0 {
		kv.Set(pairs)
	}
}

// IsNativeOp 判断是否为 VM 原生求值的算子。
func IsNativeOp(opcode string) bool { return nativeOps[opcode] }

// NativeOpList 返回所有 VM 原生算子的列表 (仅 opcode)。
func NativeOpList() []string {
	ops := make([]string, 0, len(nativeOps))
	for op := range nativeOps {
		ops = append(ops, op)
	}
	return ops
}

// OpDefs 返回格式化后的算子定义文本列表 (按 opcode 排序)。
func OpDefs() []string {
	defs := make([]string, 0, len(nativeSigs))
	for op := range nativeOps {
		if sig, ok := nativeSigs[op]; ok {
			defs = append(defs, sig)
		} else {
			defs = append(defs, "rwir "+op+"() -> ()")
		}
	}
	return defs
}

// IsUnaryNativeOp 判断是否为单目原生算子。
func IsUnaryNativeOp(opcode string) bool {
	switch opcode {
	case "!", "-", "abs", "sqrt", "exp", "log", "neg", "sign", "bool":
		return true
	}
	return false
}
