package builtin

import "kvlang/internal/kvspace"

func isRelative(param string) bool  { return len(param) >= 2 && param[:2] == "./" }
func isAbsolute(param string) bool  { return len(param) > 0 && param[0] == '/' }

func isImmediateNumber(param string) bool {
	if len(param) == 0 { return false }
	for _, c := range param {
		if c >= '0' && c <= '9' || c == '.' || c == 'e' || c == 'E' { continue }
		return false
	}
	return true
}

func isImmediateBool(param string) bool { return param == "true" || param == "false" }

// resolveWriteKey maps a write-slot param to an absolute KV key.
func resolveWriteKey(framePath, param string) string {
	if isRelative(param) { return framePath + "/" + param[2:] }
	if isAbsolute(param) { return param }
	return framePath + "/" + param
}

// ResolveReadValue maps a read-slot param to a typed Value.
// Exported for kvcpu/controlflow and layoutcode.
func ResolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.Value {
	return resolveReadValue(kv, framePath, param)
}

// resolveReadValue maps a read-slot param to a typed Value.
//
//	"X  → kvspace.Str("X")     — quoted string literal (parser writes " prefix)
//	./x  → kv.Get(framePath/x)  — explicit-relative slot
//	/abs → kv.Get(/abs)          — absolute path
//	3    → kvspace.Int(3)        — numeric literal
//	true → kvspace.Bool(true)    — bool literal
//	x    → kv.Get(framePath/x)  — bare ident (same as ./x)
func resolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.Value {
	if len(param) > 0 && param[0] == '"' {
		return kvspace.Str(param[1:])
	}
	if isRelative(param) {
		v, _ := kv.Get(framePath + "/" + param[2:])
		return v
	}
	if isAbsolute(param) {
		v, _ := kv.Get(param)
		return v
	}
	if isImmediateNumber(param) || isImmediateBool(param) {
		return parseLiteral(param)
	}
	v, _ := kv.Get(framePath + "/" + param)
	return v
}
