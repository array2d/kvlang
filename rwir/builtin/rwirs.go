package builtin

import (
	"context"
	"strings"

	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
	"kvlang/symbol"
)

// nativeRwir 内置算子注册项。
type nativeRwir struct {
	sig  string // 签名: "rwir op(reads...) -> (writes...)"
	impl Rwir     // nil 表示无实现（仅占位，非 VM 原生求值）
}

// rwirregistry opcode → nativeRwir（各 op 文件 init() 自行注册）。
var rwirregistry = map[string]nativeRwir{}

var bg = context.Background()

// Register 注册一个 native opcode：签名 + 实现。
func Register(opcode, sig string, impl Rwir) {
	rwirregistry[opcode] = nativeRwir{sig: sig, impl: impl}
}

// registerWord 注册 word 及其全部 glyph。word 是保留字，用户不可定义同名函数。
func registerWord(word, sig string, impl Rwir) {
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

// WriteRwir 将所有已注册的 native rwir 签名写入 /rwir/{runtime}/。
// rwir 是空函数体（无指令体，仅签名），只能落在 /rwir/{runtime}/，不进 /lib/。
// {runtime} 反射自可执行文件名（如 kvlang），使多个 runtime 共存于同一 kvspace 不冲突。
func WriteRwir(kv kvspace.KVSpace, runtime string) {
	pairs := make([]kvspace.KVPair, 0, len(rwirregistry))
	for opcode, op := range rwirregistry {
		if op.sig == "" { continue }
		if strings.Contains(opcode, "/") { continue }
		reads, writes := countSigParams(op.sig)
		pairs = append(pairs, kvspace.KVPair{
			Key: keytree.RwirRuntime(runtime, opcode),
			Val: kvspace.NewRwir(int32(reads), int32(writes), string(op.sig)),
		})
	}
	if len(pairs) > 0 {
		kv.Set(pairs)
	}
}

// IsNativeRwir 判断是否为 VM 原生求值的算子。
func IsNativeRwir(opcode string) bool {
	_, ok := rwirregistry[opcode]
	return ok
}

// ResolveWriteSlot 解析写槽名为 KV key（供 kvcpu handoff 回写结果）。
func ResolveWriteSlot(kv kvspace.KVSpace, framePath, name string) string {
	return resolveWriteSlot(kv, framePath, name)
}
