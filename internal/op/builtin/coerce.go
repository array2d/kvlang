package builtin

import (
	"strconv"
	"strings"

	"kvlang/internal/kvspace"
)

// asFloat coerces a Value to float64 for numeric operations.
func asFloat(v kvspace.Value) float64 {
	switch v.Kind() {
	case "int":   return float64(v.Int())
	case "float": return v.Float()
	case "bool":  if v.Bool() { return 1 }; return 0
	default:      f, _ := strconv.ParseFloat(v.Str(), 64); return f
	}
}

// asInt coerces a Value to int64.
func asInt(v kvspace.Value) int64 {
	switch v.Kind() {
	case "int":   return v.Int()
	case "float": return int64(v.Float())
	case "bool":  if v.Bool() { return 1 }; return 0
	default:      i, _ := strconv.ParseInt(v.Str(), 10, 64); return i
	}
}

// AsBool coerces a Value to bool (kvlang truth semantics).
// Exported for use by kvcpu/controlflow (br condition evaluation).
func AsBool(v kvspace.Value) bool {
	switch v.Kind() {
	case "bool":  return v.Bool()
	case "int":   return v.Int() != 0
	case "float": return v.Float() != 0
	default:      s := v.Str(); return s != "" && s != "0" && s != "false"
	}
}

// isNumeric reports whether v is int or float.
func isNumeric(v kvspace.Value) bool { return v.Kind() == "int" || v.Kind() == "float" }

// display formats a Value for human output (print / str.set).
func display(v kvspace.Value) string {
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

// parseLiteral parses an immediate operand string (from instruction slots) into a typed Value.
func parseLiteral(s string) kvspace.Value {
	switch strings.ToLower(strings.TrimSpace(s)) {
	case "true":  return kvspace.Bool(true)
	case "false": return kvspace.Bool(false)
	}
	if i, err := strconv.ParseInt(s, 10, 64); err == nil {
		return kvspace.Int(i)
	}
	if f, err := strconv.ParseFloat(s, 64); err == nil {
		return kvspace.Float(f)
	}
	return kvspace.Str(s)
}
