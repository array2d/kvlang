package builtin

import (
	"strings"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
)

func isAbsolute(param string) bool { return len(param) > 0 && param[0] == '/' }

// resolveWriteKey maps a write-slot param to an absolute KV key。
// 递归向上查找：先查 .wparam 重定向，再查变量值，函数帧边界为止。
// 若变量首次写入且当前帧为 label，写到函数帧——TCO 丢弃 label 帧时变量不丢。
func resolveWriteKey(kv kvspace.KVSpace, framePath, param string) string {
	if isAbsolute(param) { return param }
	funcFrame := framePath
	for f := framePath; f != ""; f = keytree.ParentFrame(f) {
		if extKind(kv, f) == kvspace.KindRwfunc {
			funcFrame = f
			break
		}
	}
	for f := framePath; f != ""; f = keytree.ParentFrame(f) {
		if r := kvspace.GetOne(kv, keytree.WParam(f, param)); !r.IsNil() {
			return r.Str()
		}
		if v := kvspace.GetOne(kv, keytree.Stack(f)+param); !v.IsNil() {
			return keytree.Stack(f) + param
		}
		if extKind(kv, f) == kvspace.KindRwfunc {
			break
		}
	}
	return keytree.Stack(funcFrame) + param
}

// ResolveReadValue maps a read-slot param to a typed Value.
func ResolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.XValue {
	return resolveReadValue(kv, framePath, param)
}

// resolveReadValue maps a read-slot param to a typed Value.
// 递归向上查找：当前帧 → 父帧 → ... → 函数帧边界（extKind="rwfunc"）。
func resolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.XValue {
	if len(param) == 0 {
		return kvspace.XValue{}
	}
	if param[0] == '"' {
		return kvspace.Str(param[1:])
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
	// 递归向上查找，每层先查 .rparam 重定向，再查变量值
	for f := framePath; f != ""; f = keytree.ParentFrame(f) {
		if r := kvspace.GetOne(kv, keytree.RParam(f, param)); !r.IsNil() {
			return kvspace.GetOne(kv, r.Str())
		}
		if v := kvspace.GetOne(kv, keytree.Stack(f)+param); !v.IsNil() {
			return v
		}
		if extKind(kv, f) == kvspace.KindRwfunc {
			break
		}
	}
	return kvspace.XValue{}
}

// extKind 读帧根 extindex target 的 XValue.Kind()。
func extKind(kv kvspace.KVSpace, frameRoot string) string {
	trimmed := strings.TrimRight(frameRoot, keytree.PathSegSep)
	if trimmed == "" || trimmed == keytree.PathSegSep {
		return ""
	}
	parent, dirName := kvspace.SepPath(trimmed)
	if parent != keytree.PathSegSep {
		parent += kvspace.DirIndexSuf
	}
	extVal := kv.Get(parent, []string{dirName + kvspace.DirIndexSuf})[0]
	_, extTarget := kvspace.DecodeExtIndex(extVal)
	if extTarget == "" {
		return ""
	}
	return kvspace.GetOne(kv, strings.TrimRight(extTarget, keytree.PathSegSep)).Kind()
}
