package builtin

import (
	"oldhero/keytree"
	"github.com/array2d/kvspace-go"
	"oldhero/rwir"
)

func isAbsolute(param string) bool { return len(param) > 0 && param[0] == '/' }

// writeSlotKey 返回 slot 在 rwfunc 帧的绝对 KV key。
func writeSlotKey(kv kvspace.KVSpace, framePath, slot string) string {
	if isAbsolute(slot) { return slot }
	return keytree.Stack(funcFrameRoot(kv, framePath)) + slot
}

// ResolveReadValue maps a read-slot param to a typed Value.
func ResolveReadValue(kv kvspace.KVSpace, framePath string, param rwir.Param) kvspace.XValue {
	return resolveReadValue(kv, framePath, param)
}

// resolveReadValue 从 rwfunc 帧查读参值。
// 字面量（Kind ≠ rwir/rwfunc）→ 直接返 Val。变量引用 → 帧查找。
// 变量查找优先走命名 Ptr（lib layout 阶段的 name→slot 映射）：
//
//	frameRoot/a → ext→ Ptr("[0,-1]")             ← 1跳: name→slot
//	frameRoot/[0,-1] → Char("/vthread/1/x")      ← 2跳: slot→arg地址
//	GetOne("/vthread/1/x") → value                ← 3跳: 解引用
//
// 由于 HandleCall 的 arg 可能再经 Ptr 链传递，argAddr 指向的值可能仍是 Char（路径重定向），
// 需要循环解引用直到非 Char 类型。
func resolveReadValue(kv kvspace.KVSpace, framePath string, param rwir.Param) kvspace.XValue {
	if !kvspace.IsNone(param.Val) && param.Val.Kind() != kvspace.KindRwir && param.Val.Kind() != kvspace.KindRwfunc {
		return param.Val
	}
	name := param.Name
	if len(name) == 0 {
		return kvspace.None{}
	}
	if isAbsolute(name) {
		return kvspace.GetOne(kv, name)
	}
	rwRoot := funcFrameRoot(kv, framePath)
	// 命名 Ptr：frameRoot/name → Ptr → 读 slot → 一级解引用
	// resolveReadPath 已沿 Char 链跟到底，slot 里存的是最终目标路径。
	if ptrVal := kvspace.GetOne(kv, keytree.Stack(rwRoot)+name); kvspace.IsPtr(ptrVal) {
		argAddr := kvspace.GetOne(kv, keytree.Stack(rwRoot)+kvspace.PtrTarget(ptrVal))
		if !kvspace.IsNone(argAddr) {
			return kvspace.GetOne(kv, argAddr.ValueString())
		}
	}
	// fallback: 帧内局部变量
	if v := kvspace.GetOne(kv, keytree.Stack(rwRoot)+name); !kvspace.IsNone(v) && !kvspace.IsPtr(v) {
		return v
	}
	return kvspace.None{}
}
