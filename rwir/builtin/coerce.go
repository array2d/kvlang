package builtin

import (
	"strconv"
	"strings"

	"github.com/array2d/kvspace-go"
)

// asFloat coerces a numeric Value to float64.
func asFloat(v kvspace.XValue) float64 {
	switch v := v.(type) {
	case kvspace.Int8: return float64(v.At(0))
	case kvspace.Int16: return float64(v.At(0))
	case kvspace.Int32: return float64(v.At(0))
	case kvspace.Int64: return float64(v.At(0))
	case kvspace.Uint8: return float64(v.At(0))
	case kvspace.Uint16: return float64(v.At(0))
	case kvspace.Uint32: return float64(v.At(0))
	case kvspace.Uint64: return float64(v.At(0))
	case kvspace.Float32: return float64(v.At(0))
	case kvspace.Float64: return v.At(0)
	default:
		panic("asFloat: cannot coerce " + v.Kind() + " to float — expected numeric kind")
	}
}

// asInt64 coerces a numeric Value to int64.
func asInt64(v kvspace.XValue) int64 {
	switch v := v.(type) {
	case kvspace.Int8: return int64(v.At(0))
	case kvspace.Int16: return int64(v.At(0))
	case kvspace.Int32: return int64(v.At(0))
	case kvspace.Int64: return v.At(0)
	case kvspace.Uint8: return int64(v.At(0))
	case kvspace.Uint16: return int64(v.At(0))
	case kvspace.Uint32: return int64(v.At(0))
	case kvspace.Uint64: return int64(v.At(0))
	case kvspace.Float32: return int64(v.At(0))
	case kvspace.Float64: return int64(v.At(0))
	case kvspace.Time: return v.At(0)
	case kvspace.Duration: return v.At(0)
	default:
		panic("asInt64: cannot coerce " + v.Kind() + " to int — expected numeric kind")
	}
}

// AsBool coerces a Value to bool.
func AsBool(v kvspace.XValue) bool {
	if v.Kind() != "bool" {
		panic("AsBool: expected bool kind, got " + v.Kind() + " — use explicit comparison (e.g. x != 0) instead of bare value")
	}
	return v.(kvspace.Bool).At(0)
}

// isNumeric reports whether v is int or float.
func isNumeric(v kvspace.XValue) bool { return isIntKind(v.Kind()) || isFloatKind(v.Kind()) }

// display formats a Value for human output (print / string.set).
func display(v kvspace.XValue) string {
	if v.Kind() == "stringbyte" {
		return v.ValueString()
	}
	if v.ArrayLen() > 1 {
		return formatArray(v)
	}
	return v.ValueString()
}

// xvalueAt returns the i-th element of any XValue, or None{}.
func xvalueAt(v kvspace.XValue, i int) kvspace.XValue {
	n := int(v.ArrayLen())
	if i < 0 || i >= n {
		return kvspace.None{}
	}
	switch v := v.(type) {
	case kvspace.Int8: return kvspace.NewInt8(v.At(i))
	case kvspace.Int16: return kvspace.NewInt16(v.At(i))
	case kvspace.Int32: return kvspace.NewInt32(v.At(i))
	case kvspace.Int64: return kvspace.NewInt64(v.At(i))
	case kvspace.Uint8: return kvspace.NewUint8(v.At(i))
	case kvspace.Uint16: return kvspace.NewUint16(v.At(i))
	case kvspace.Uint32: return kvspace.NewUint32(v.At(i))
	case kvspace.Uint64: return kvspace.NewUint64(v.At(i))
	case kvspace.Float32: return kvspace.NewFloat32(v.At(i))
	case kvspace.Float64: return kvspace.NewFloat64(v.At(i))
	case kvspace.Bool: return kvspace.NewBool(v.At(i))
	default:
		data := v.Encode()
		start := int(kvspace.DecodeXValueHead(data).HeadLen())
		for j := 0; j < i; j++ {
			h := kvspace.DecodeXValueHead(data[start:])
			if h.Kind == "" {
				return kvspace.None{}
			}
			start += int(h.HeadLen()) + int(h.BodyLen)
		}
		return kvspace.DecodeXValue(data[start:])
	}
}

// rawBytesOf 返回 XValue 的 body 原始字节。
func rawBytesOf(v kvspace.XValue) []byte {
	return kvspace.BodyBytes(v)
}

// formatArray 格式化 len>1 的 XValue。
func formatArray(v kvspace.XValue) string {
	n := int(v.ArrayLen())
	parts := make([]string, n)
	for i := 0; i < n; i++ {
		parts[i] = display(xvalueAt(v, i))
	}
	return "[" + strings.Join(parts, ", ") + "]"
}

// TryParseNumber attempts to interpret s as a numeric literal.
// Tries int64 first, then uint64 for overflow, then float64.
func TryParseNumber(s string) (kvspace.XValue, bool) {
	if len(s) == 0 {
		return kvspace.None{}, false
	}
	c0 := s[0]
	switch {
	case c0 >= '0' && c0 <= '9':
	case c0 == '-' && len(s) >= 2 && s[1] >= '0' && s[1] <= '9':
	default:
		return kvspace.None{}, false
	}
	if i, err := strconv.ParseInt(s, 10, 64); err == nil {
		return kvspace.NewInt64(i), true
	}
	if c0 != '-' && !strings.ContainsAny(s, ".eE") {
		if u, err := strconv.ParseUint(s, 10, 64); err == nil {
			return kvspace.NewUint64(u), true
		}
	}
	if f, err := strconv.ParseFloat(s, 64); err == nil {
		return kvspace.NewFloat64(f), true
	}
	return kvspace.None{}, false
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

func isUnsignedKind(k string) bool {
	switch k {
	case "uint8", "uint16", "uint32", "uint64":
		return true
	}
	return false
}

func intKindWidth(k string) int {
	switch k {
	case "int8", "uint8":
		return 8
	case "int16", "uint16":
		return 16
	case "int32", "uint32":
		return 32
	case "int64", "uint64":
		return 64
	}
	return 0
}

func widerIntKind(ak, bk string) string {
	aw, bw := intKindWidth(ak), intKindWidth(bk)
	au, bu := isUnsignedKind(ak), isUnsignedKind(bk)
	if au && bu {
		if aw >= bw {
			return ak
		}
		return bk
	}
	if !au && !bu {
		if aw >= bw {
			return ak
		}
		return bk
	}
	w := aw
	if bw > w {
		w = bw
	}
	switch w {
	case 8:
		return "int16"
	case 16:
		return "int32"
	case 32:
		return "int64"
	default:
		return "int64"
	}
}

func widerFloatKind(ak, bk string) string {
	if ak == "float64" || bk == "float64" {
		return "float64"
	}
	if ak == "float32" || bk == "float32" {
		return "float32"
	}
	return "float64"
}

func narrowInt(ak, bk string, v int64) kvspace.XValue {
	kind := widerIntKind(ak, bk)
	switch kind {
	case "int8":
		return kvspace.NewInt8(int8(v))
	case "int16":
		return kvspace.NewInt16(int16(v))
	case "int32":
		return kvspace.NewInt32(int32(v))
	case "int64":
		return kvspace.NewInt64(v)
	case "uint8":
		return kvspace.NewUint8(uint8(v))
	case "uint16":
		return kvspace.NewUint16(uint16(v))
	case "uint32":
		return kvspace.NewUint32(uint32(v))
	case "uint64":
		return kvspace.NewUint64(uint64(v))
	default:
		return kvspace.NewInt64(v)
	}
}

func narrowFloat(ak, bk string, v float64) kvspace.XValue {
	if widerFloatKind(ak, bk) == "float32" {
		return kvspace.NewFloat32(float32(v))
	}
	return kvspace.NewFloat64(v)
}

// rawDecodeN 从原始字节创建指定 kind 的 XValue。
func rawDecodeN(kind string, raw []byte, n int32) kvspace.XValue {
	switch kind {
	case "bool":
		return kvspace.DecodeBool(raw)
	case "int8":
		return kvspace.DecodeInt8(raw)
	case "int16":
		return kvspace.DecodeInt16(raw)
	case "int32":
		return kvspace.DecodeInt32(raw)
	case "int64":
		return kvspace.DecodeInt64(raw)
	case "uint8":
		return kvspace.DecodeUint8(raw)
	case "uint16":
		return kvspace.DecodeUint16(raw)
	case "uint32":
		return kvspace.DecodeUint32(raw)
	case "uint64":
		return kvspace.DecodeUint64(raw)
	case "float32":
		return kvspace.DecodeFloat32(raw)
	case "float64":
		return kvspace.DecodeFloat64(raw)
	default:
		panic("rawDecodeN: unknown kind " + kind)
	}
}


