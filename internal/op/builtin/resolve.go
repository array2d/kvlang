package builtin

import "github.com/array2d/kvspace-go"

func isAbsolute(param string) bool { return len(param) > 0 && param[0] == '/' }

// resolveWriteKey maps a write-slot param to an absolute KV key.
// 写参重定向：若帧有 .wparam/<param>，返回其存储的绝对路径（零拷贝直写父帧）。
func resolveWriteKey(kv kvspace.KVSpace, framePath, param string) string {
	if isAbsolute(param) { return param }
	if r, _ := kv.Get(framePath + "/.wparam/" + param); !r.IsNil() {
		return r.Str()
	}
	return framePath + "/" + param
}

// ResolveReadValue maps a read-slot param to a typed Value.
// Exported for kvcpu/controlflow and layoutrwir.
func ResolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.XValue {
	return resolveReadValue(kv, framePath, param)
}

// resolveReadValue maps a read-slot param to a typed Value.
//
//	"X   → kvspace.Str("X")      quoted string literal  (parser writes " prefix)
//	/abs → kv.Get(/abs)           absolute path
//	true → kvspace.Bool(true)     bool literal           (exact match)
//	42   → kvspace.Int(42)        numeric literal        (first-char + strconv)
//	x    → kv.Get(framePath/x)   bare ident / slot reference
func resolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.XValue {
	if len(param) == 0 {
		return kvspace.XValue{}
	}
	if param[0] == '"' {
		return kvspace.Str(param[1:])
	}
	if isAbsolute(param) {
		v, _ := kv.Get(param)
		return v
	}
	// bool literals: exact match (parser always emits lowercase "true"/"false")
	if param == "true"  { return kvspace.Bool(true) }
	if param == "false" { return kvspace.Bool(false) }
	// numeric literals: first-char fast rejection, then delegate to strconv
	if v, ok := tryParseNumber(param); ok {
		return v
	}
	// malformed numeric literal (starts with digit but ParseFloat failed, e.g. "1e").
	// Parser should have caught this and issued a diagnostic; runtime returns zero.
	if len(param) > 0 && param[0] >= '0' && param[0] <= '9' {
		return kvspace.XValue{} // invalid number → zero value, not a bare ident
	}
	// bare identifier → check redirect first, then slot in current frame
	// 读参重定向：帧 .rparam/<param> → 调用方值位置（零拷贝读）
	// 写参重定向：帧 .wparam/<param> → 调用方写目标（零拷贝读写）
	if r, err := kv.Get(framePath + "/.rparam/" + param); err == nil && !r.IsNil() {
		v, _ := kv.Get(r.Str())
		return v
	}
	if r, err := kv.Get(framePath + "/.wparam/" + param); err == nil && !r.IsNil() {
		v, _ := kv.Get(r.Str())
		return v
	}
	v, _ := kv.Get(framePath + "/" + param)
	return v
}
