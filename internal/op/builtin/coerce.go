package builtin

import (
	"strconv"
	"strings"

	"kvlang/internal/kvspace"
)

// asFloat coerces a Value to float64 for numeric operations.
func asFloat(v kvspace.XValue) float64 {
	switch v.Kind() {
	case "int":   return float64(v.Int())
	case "float": return v.Float()
	case "bool":  if v.Bool() { return 1 }; return 0
	default:      f, _ := strconv.ParseFloat(v.Str(), 64); return f
	}
}

// asInt coerces a Value to int64.
func asInt(v kvspace.XValue) int64 {
	switch v.Kind() {
	case "int":   return v.Int()
	case "float": return int64(v.Float())
	case "bool":  if v.Bool() { return 1 }; return 0
	default:      i, _ := strconv.ParseInt(v.Str(), 10, 64); return i
	}
}

// AsBool coerces a Value to bool (kvlang truth semantics).
// Exported for use by kvcpu/controlflow (br condition evaluation).
func AsBool(v kvspace.XValue) bool {
	switch v.Kind() {
	case "bool":  return v.Bool()
	case "int":   return v.Int() != 0
	case "float": return v.Float() != 0
	default:      s := v.Str(); return s != "" && s != "0" && s != "false"
	}
}

// isNumeric reports whether v is int or float.
func isNumeric(v kvspace.XValue) bool { return v.Kind() == "int" || v.Kind() == "float" }

// display formats a Value for human output (print / string.set).
func display(v kvspace.XValue) string {
	switch v.Kind() {
	case "int":   return strconv.FormatInt(v.Int(), 10)
	case "float":
		s := strconv.FormatFloat(v.Float(), 'f', -1, 64)
		if !strings.Contains(s, ".") { s += ".0" }
		return s
	case "bool":  return strconv.FormatBool(v.Bool())
	default:      return v.Str()
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
	if f, err := strconv.ParseFloat(s, 64); err == nil {
		return kvspace.Float(f), true
	}
	return kvspace.XValue{}, false
}
