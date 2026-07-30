package builtin

import (
	"context"
	"strings"

	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
	"kvlang/symbol"
)

// nativeOp 内置算子注册项。
type nativeOp struct {
	sig  string // 签名: "rwir op(reads...) -> (writes...)"
	impl Op     // nil 表示无实现（仅占位，非 VM 原生求值）
}

// registry opcode → nativeOp（各 op 文件 init() 自行注册）。
var registry = map[string]nativeOp{}

var bg = context.Background()

// Register 注册一个 native opcode：签名 + 实现。
func Register(opcode, sig string, impl Op) {
	registry[opcode] = nativeOp{sig: sig, impl: impl}
}

// registerWord 注册 word 及其全部 glyph。word 是保留字，用户不可定义同名函数。
func registerWord(word, sig string, impl Op) {
	Register(word, sig, impl)
	e := symbol.ByWord(word)
	for _, g := range e.Glyphs {
		if g != word {
			Register(g, strings.Replace(sig, word, g, 1), impl)
		}
	}
}

// countSigParams 从 "rwir name(params) -> (returns)" 签名字符串中解析读写参数量。
func countSigParams(sig string) (reads, writes int32) {
	// 找到 -> 分割
	arrow := strings.Index(sig, "->")
	if arrow < 0 { return 0, 0 }
	// 读参：sig 开头到 -> 之间的括号内容
	readsPart := sig[:arrow]
	if lp := strings.Index(readsPart, "("); lp >= 0 {
		if rp := strings.Index(readsPart, ")"); rp > lp {
			inner := readsPart[lp+1 : rp]
			if strings.TrimSpace(inner) != "" {
				reads = 1 + int32(strings.Count(inner, ","))
			}
		}
	}
	// 写参：-> 之后到末尾的括号内容
	writesPart := sig[arrow+2:]
	if lp := strings.Index(writesPart, "("); lp >= 0 {
		if rp := strings.Index(writesPart, ")"); rp > lp {
			inner := writesPart[lp+1 : rp]
			if strings.TrimSpace(inner) != "" {
				writes = 1 + int32(strings.Count(inner, ","))
			}
		}
	}
	return
}

// WriteSysRwir 将所有已注册的 native rwir 签名写入 /sys/rwir/。
// 基础类型（+, ==, print 等）→ /sys/rwir/opcode
// string.* 类型（string.len, string.char 等）→ /sys/rwir/string.opcode
// 应在 runtime 启动时调用（非 parser 阶段）。
func WriteSysRwir(kv kvspace.KVSpace) {
	pairs := make([]kvspace.KVPair, 0, len(registry))
	for opcode, op := range registry {
		if op.sig == "" { continue }
		if strings.Contains(opcode, "/") { continue }
		reads, writes := countSigParams(op.sig)
		pairs = append(pairs, kvspace.KVPair{
			Key: keytree.SysRwir(opcode),
			Val: kvspace.Rwir(reads, writes, []byte(op.sig)),
		})
	}
	if len(pairs) > 0 {
		kv.Set(pairs)
	}
}

// IsNativeOp 判断是否为 VM 原生求值的算子。
func IsNativeOp(opcode string) bool {
	_, ok := registry[opcode]
	return ok
}

// NativeOpList 返回所有 VM 原生算子的列表 (仅 opcode)。
func NativeOpList() []string {
	ops := make([]string, 0, len(registry))
	for op := range registry {
		ops = append(ops, op)
	}
	return ops
}

// OpDefs 返回格式化后的算子定义文本列表 (按 opcode 排序)。
func OpDefs() []string {
	defs := make([]string, 0, len(registry))
	for opcode, op := range registry {
		if op.sig != "" {
			defs = append(defs, op.sig)
		} else {
			defs = append(defs, "rwir "+opcode+"() -> ()")
		}
	}
	return defs
}

// IsUnaryNativeOp 判断是否为单目原生算子。
func IsUnaryNativeOp(opcode string) bool {
	if symbol.Lookup(opcode).Unary { return true }
	switch opcode {
	case "abs", "sqrt", "exp", "log", "neg", "sign", "bool":
		return true
	}
	return false
}
