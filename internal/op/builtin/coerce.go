package builtin

import (
	"strconv"
	"strings"

	"github.com/array2d/kvlang-go"
)

// nilAsInt 将 nil 在数值语境按 int 0 参与（fix-017 方案 A，与 nil==0 比较语义一致）。
func nilAsInt(v kvspace.XValue) kvspace.XValue {
	if v.IsNil() { return kvspace.Int(0) }
	return v
}

// asFloat coerces a Value to float64 for numeric operations.
func asFloat(v kvspace.XValue) float64 {
	switch v.Kind() {
	case "int", "int8", "int16", "int32", "int64":
		return float64(v.Int64())
	case "uint8", "uint16", "uint32", "uint64":
		return float64(v.Uint64())
	case "float", "float32":
		return float64(v.Float32())
	case "float64":
		return v.Float64()
	case "bool":
		if v.Bool() { return 1 }; return 0
	default:
		f, _ := strconv.ParseFloat(v.Str(), 64); return f
	}
}

// asInt coerces a Value to int64.
func asInt(v kvspace.XValue) int64 {
	switch v.Kind() {
	case "int", "int8", "int16", "int32", "int64":
		return v.Int64()
	case "uint8", "uint16", "uint32", "uint64":
		return int64(v.Uint64())
	case "float", "float32":
		return int64(v.Float32())
	case "float64":
		return int64(v.Float64())
	case "bool":
		if v.Bool() { return 1 }; return 0
	default:
		i, _ := strconv.ParseInt(v.Str(), 10, 64); return i
	}
}

// AsBool coerces a Value to bool (kvlang truth semantics).
// Exported for use by kvcpu/controlflow (br condition evaluation).
func AsBool(v kvspace.XValue) bool {
	if v.IsNil() { return false }
	switch v.Kind() {
	case "bool": return v.Bool()
	case "int", "int8", "int16", "int32", "int64":
		return v.Int64() != 0
	case "uint8", "uint16", "uint32", "uint64":
		return v.Uint64() != 0
	case "float", "float32":
		return v.Float32() != 0
	case "float64":
		return v.Float64() != 0
	default:
		s := v.Str(); return s != "" && s != "0" && s != "false"
	}
}

// isNumeric reports whether v is int or float.
func isNumeric(v kvspace.XValue) bool { return isIntKind(v.Kind()) || isFloatKind(v.Kind()) }

// display formats a Value for human output (print / string.set).
func display(v kvspace.XValue) string {
	switch v.Kind() {
	case "int", "int8", "int16", "int32", "int64":
		return strconv.FormatInt(v.Int64(), 10)
	case "uint8", "uint16", "uint32", "uint64":
		return strconv.FormatUint(v.Uint64(), 10)
	case "float", "float32":
		s := strconv.FormatFloat(float64(v.Float32()), 'f', -1, 32)
		if !strings.Contains(s, ".") { s += ".0" }
		return s
	case "float64":
		s := strconv.FormatFloat(v.Float64(), 'f', -1, 64)
		if !strings.Contains(s, ".") { s += ".0" }
		return s
	case "bool": return strconv.FormatBool(v.Bool())
	case "string": return v.Str()
	case "array": return v.String() // debug format: array:NNB
	default: return v.String()
	}
}

// tryParseNumber attempts to interpret s as a numeric literal.
// Returns (value, true) on success; (zero, false) if s is not numeric.
//
// Design follows mainstream language runtimes (Go scanner, Python tokenizer,
// Rust rustc_lexer): check only the first character for fast rejection, then
// delegate actual parsing to strconv — the authoritative implementation.
//
//   "is it a number?" → first-char check  (O(1), no false positives)
//   "what's the value?" → strconv          (correct for all IEEE 754 forms)
//
// kvlang note: the parser merges unary '-' with digit literals into a single
// Leaf("-42"), so negative literals are handled here as a special case.
// All other languages treat '-' as a separate unary-operator token.
//
// Accepts: "42"  "-7"  "3.14"  "1e10"  "-1.5e-3"
// Rejects: "e"   "."   "-"     "abc"   ""
func tryParseNumber(s string) (kvspace.XValue, bool) {
	if len(s) == 0 {
		return kvspace.XValue{}, false
	}
	c0 := s[0]
	switch {
	case c0 >= '0' && c0 <= '9':
		// positive literal: integer, float, or scientific notation
	case c0 == '-' && len(s) >= 2 && s[1] >= '0' && s[1] <= '9':
		// negative literal: kvlang parser folds "-" + digit → Leaf("-42")
	default:
		return kvspace.XValue{}, false
	}
	// Delegate to stdlib — handles all edge cases including scientific notation.
	if i, err := strconv.ParseInt(s, 10, 64); err == nil {
		return kvspace.Int(i), true
	}
	// (2^63, 2^64-1] 区间的无小数正整数字面量 → uint64（如 uint64 上界 18446744073709551615）
	if c0 != '-' && !strings.ContainsAny(s, ".eE") {
		if u, err := strconv.ParseUint(s, 10, 64); err == nil {
			return kvspace.Uint64(u), true
		}
	}
	if f, err := strconv.ParseFloat(s, 64); err == nil {
		return kvspace.Float(f), true
	}
	return kvspace.XValue{}, false
}

func isIntKind(k string) bool {
	switch k {
	case "int", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64":
		return true
	}
	return false
}

func isFloatKind(k string) bool {
	return k == "float" || k == "float32" || k == "float64"
}
