package builtin

import (
	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
)

func isAbsolute(param string) bool { return len(param) > 0 && param[0] == '/' }

// writeSlotKey 返回 slot 在 rwfunc 帧的绝对 KV key。
func writeSlotKey(kv kvspace.KVSpace, framePath, slot string) string {
	if isAbsolute(slot) { return slot }
	return keytree.Stack(funcFrameRoot(kv, framePath)) + slot
}

// ResolveReadValue maps a read-slot param to a typed Value.
func ResolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.XValue {
	return resolveReadValue(kv, framePath, param)
}

// resolveReadValue 从 rwfunc 帧查读参值。
// 先查 .rparam 重定向，再查 rwfunc Stack 变量，最后查当前帧。
func resolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.XValue {
	if len(param) == 0 {
		return kvspace.XValue{}
	}
	if param[0] == '"' {
		return kvspace.String(param[1:])
	}
	if isAbsolute(param) {
		return kvspace.GetOne(kv, param)
	}
	if param == "true" {
		return kvspace.Bool(true)
	}
	if param == "false" {
		return kvspace.Bool(false)
	}
	if param == "null" {
		return kvspace.XValue{}
	}
	if v, ok := tryParseNumber(param); ok {
		return v
	}
	if len(param) > 0 && param[0] >= '0' && param[0] <= '9' {
		return kvspace.XValue{}
	}
	// scope/function 帧统一：查 rwfunc 帧 .rparam 与 Stack 变量
	rwRoot := funcFrameRoot(kv, framePath)
	if r := kvspace.GetOne(kv, keytree.RParam(rwRoot, param)); !r.IsNone() {
		return kvspace.GetOne(kv, r.Str())
	}
	if v := kvspace.GetOne(kv, keytree.Stack(rwRoot)+param); !v.IsNone() {
		return v
	}
	return kvspace.XValue{}
}
