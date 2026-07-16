package builtin

import "kvlang/internal/kvspace"

func isRelative(param string) bool { return len(param) >= 2 && param[:2] == "./" }
func isAbsolute(param string) bool { return len(param) > 0 && param[0] == '/' }

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
//	"X   → kvspace.Str("X")      quoted string literal  (parser writes " prefix)
//	./x  → kv.Get(framePath/x)   explicit-relative slot
//	/abs → kv.Get(/abs)           absolute path
//	true → kvspace.Bool(true)     bool literal           (exact match)
//	42   → kvspace.Int(42)        numeric literal        (first-char + strconv)
//	x    → kv.Get(framePath/x)   bare ident / slot reference
func resolveReadValue(kv kvspace.KVSpace, framePath, param string) kvspace.Value {
	if len(param) == 0 {
		return kvspace.Value{}
	}
	if param[0] == '"' {
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
		return kvspace.Value{} // invalid number → zero value, not a bare ident
	}
	// bare identifier → slot in current frame
	v, _ := kv.Get(framePath + "/" + param)
	return v
}
