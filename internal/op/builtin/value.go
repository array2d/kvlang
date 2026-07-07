package builtin

import (
	"strconv"
	"strings"
)

// nativeValue 表示 VM 原生求值中的值，支持 bool / int / float / string。
type nativeValue struct {
	kind string // "bool" | "int" | "float" | "string"
	raw  string
	b    bool
	i    int64
	f    float64
}

func parseNativeValue(raw string) nativeValue {
	v := nativeValue{raw: raw}
	switch strings.ToLower(strings.TrimSpace(raw)) {
	case "true":
		v.kind = "bool"
		v.b = true
		return v
	case "false":
		v.kind = "bool"
		v.b = false
		return v
	}
	if i, err := strconv.ParseInt(raw, 10, 64); err == nil {
		v.kind = "int"
		v.i = i
		return v
	}
	if f, err := strconv.ParseFloat(raw, 64); err == nil {
		v.kind = "float"
		v.f = f
		return v
	}
	v.kind = "string"
	return v
}

func (v nativeValue) String() string {
	switch v.kind {
	case "bool":
		if v.b {
			return "true"
		}
		return "false"
	case "int":
		return strconv.FormatInt(v.i, 10)
	case "float":
		s := strconv.FormatFloat(v.f, 'f', -1, 64)
		if !strings.Contains(s, ".") {
			s += ".0"
		}
		return s
	default:
		return v.raw
	}
}

func (v nativeValue) asFloat() float64 {
	switch v.kind {
	case "int":
		return float64(v.i)
	case "float":
		return v.f
	default:
		return 0
	}
}

func (v nativeValue) asInt() int64 {
	switch v.kind {
	case "int":
		return v.i
	case "float":
		return int64(v.f)
	default:
		return 0
	}
}

func (v nativeValue) asBool() bool {
	switch v.kind {
	case "bool":
		return v.b
	default:
		return v.raw != "" && v.raw != "0"
	}
}
