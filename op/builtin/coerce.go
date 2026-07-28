package builtin

import (
	"strconv"
	"strings"

	"github.com/array2d/kvspace-go"
)

// asFloat coerces a numeric Value to float64.
// 铁律：仅接受具体位宽数字类型；非数字 kind → panic（同 AsBool 模式）。
func asFloat(v kvspace.XValue) float64 {
	switch v.Kind() {
	case "int8", "int16", "int32", "int64":
		return float64(v.Int64())
	case "uint8", "uint16", "uint32", "uint64":
		return float64(v.Uint64())
	case "float32":
		return float64(v.Float32())
	case "float64":
		return v.Float64()
	default:
		panic("asFloat: cannot coerce " + v.Kind() + " to float — expected numeric kind")
	}
}

// asInt coerces a numeric Value to int64（float→int 截断向零，五语言一致）。
// 铁律：仅接受具体位宽数字类型；非数字 kind → panic。
func asInt(v kvspace.XValue) int64 {
	switch v.Kind() {
	case "int8", "int16", "int32", "int64":
		return v.Int64()
	case "uint8", "uint16", "uint32", "uint64":
		return int64(v.Uint64())
	case "float32":
		return int64(v.Float32())
	case "float64":
		return int64(v.Float64())
	default:
		panic("asInt: cannot coerce " + v.Kind() + " to int — expected numeric kind")
	}
}

// AsBool coerces a Value to bool (kvlang truth semantics).
// Exported for use by kvcpu/controlflow (br condition evaluation).
//
// 铁律：bool 只能是 true/false，禁止隐式 coerce。
// int/float/string 不可当 bool 用——必须显式写 != 0、!= ""。
func AsBool(v kvspace.XValue) bool {
	if v.Kind() != "bool" {
		panic("AsBool: expected bool kind, got " + v.Kind() + " — use explicit comparison (e.g. x != 0) instead of bare value")
	}
	return v.Bool()
}

// isNumeric reports whether v is int or float.
func isNumeric(v kvspace.XValue) bool { return isIntKind(v.Kind()) || isFloatKind(v.Kind()) }

// display formats a Value for human output (print / string.set).
func display(v kvspace.XValue) string {
	if v.ArrayLen() > 1 {
		return formatArray(v)
	}
	switch v.Kind() {
	case "int8", "int16", "int32", "int64":
		return strconv.FormatInt(v.Int64(), 10)
	case "uint8", "uint16", "uint32", "uint64":
		return strconv.FormatUint(v.Uint64(), 10)
	case "float32":
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

// formatArray 格式化 len>1 的 XValue，显示所有元素。
func formatArray(v kvspace.XValue) string {
	n := v.Len()
	parts := make([]string, n)
	for i := 0; i < n; i++ {
		elem := v.Index(i)
		parts[i] = display(elem)
	}
	return "[" + strings.Join(parts, ", ") + "]"
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
		return kvspace.Int64(i), true
	}
	// (2^63, 2^64-1] 区间的无小数正整数字面量 → uint64（如 uint64 上界 18446744073709551615）
	if c0 != '-' && !strings.ContainsAny(s, ".eE") {
		if u, err := strconv.ParseUint(s, 10, 64); err == nil {
			return kvspace.Uint64(u), true
		}
	}
	if f, err := strconv.ParseFloat(s, 64); err == nil {
		return kvspace.Float64(f), true
	}
	return kvspace.XValue{}, false
}

func isIntKind(k string) bool {
	switch k {
	case "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64":
		return true
	}
	return false
}

func isFloatKind(k string) bool {
	return k == "float32" || k == "float64"
}
