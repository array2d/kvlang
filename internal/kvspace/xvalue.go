package kvspace

import (
	"encoding/binary"
	"strconv"
)

// XValue 是 kvspace 中存储的类型化值。
//   - 零值（IsNil()==true）表示"不存在"或"未初始化"。
//   - 一旦由构造函数创建，字段不可修改（逻辑不可变）。
//   - raw 字节由 XValue 自身 owned，不与外部缓冲区共享。
type XValue struct {
	kind string // vtype name
	raw  []byte // 类型化原始字节
}

// ── 判断 ─────────────────────────────────────────────────────────────────

func (v XValue) IsNil() bool  { return v.kind == "" || v.kind == "null" }
func (v XValue) Kind() string { return v.kind }

// Null returns the explicit null XValue.
func Null() XValue { return XValue{kind: "null"} }

// RawBytes 返回底层原始字节（任意 kind）。不拷贝，调用方不得修改。
func (v XValue) RawBytes() []byte { return v.raw }

// Raw 构造任意 vtype 的 XValue（用于第三方 vtype 扩展，如 "tensor"）。
// raw 会被复制，调用方可安全复用原缓冲区。
func Raw(kind string, raw []byte) XValue {
	c := make([]byte, len(raw))
	copy(c, raw)
	return XValue{kind: kind, raw: c}
}

// ── Stringer ─────────────────────────────────────────────────────────────

// String 实现 fmt.Stringer，输出 "kind:repr" 调试格式。
// 获取 string 类型内容请用 v.Str()。
func (v XValue) String() string {
	switch v.kind {
	case "", "null":
		return "null"
	case "int8", "int16", "int32", "int64":
		return v.kind + ":" + strconv.FormatInt(v.Int64(), 10)
	case "uint8", "uint16", "uint32", "uint64":
		return v.kind + ":" + strconv.FormatUint(v.Uint64(), 10)
	case "float32":
		return "float32:" + strconv.FormatFloat(float64(v.Float32()), 'f', -1, 32)
	case "float64":
		return "float64:" + strconv.FormatFloat(v.Float64(), 'f', -1, 64)
	case "int":
		return "int:" + strconv.FormatInt(v.Int64(), 10)
	case "float":
		return "float:" + strconv.FormatFloat(v.Float64(), 'f', -1, 64)
	case "bool":
		return "bool:" + strconv.FormatBool(v.Bool())
	case "string":
		return "string:" + v.Str()
	default:
		return v.kind + ":" + strconv.Itoa(len(v.raw)) + "B"
	}
}

// ── TLV 编解码 ───────────────────────────────────────────────────────────
//
// 格式：[1B kind_len][N B kind_name][4B raw_len LE][M B raw_value]
// IsNil() 编码为 nil（零字节）。

func EncodeXValue(v XValue) []byte {
	if v.IsNil() { return nil }
	buf := make([]byte, 1+len(v.kind)+4+len(v.raw))
	buf[0] = byte(len(v.kind))
	copy(buf[1:], v.kind)
	binary.LittleEndian.PutUint32(buf[1+len(v.kind):], uint32(len(v.raw)))
	copy(buf[1+len(v.kind)+4:], v.raw)
	return buf
}

func DecodeXValue(data []byte) XValue {
	if len(data) == 0 { return XValue{} }
	kindLen := int(data[0])
	if len(data) < 1+kindLen+4 { return XValue{} }
	kind := string(data[1 : 1+kindLen])
	if !isValidKind(kind) { return XValue{} }
	rawLen := binary.LittleEndian.Uint32(data[1+kindLen : 1+kindLen+4])
	start := 1 + kindLen + 4
	if len(data) < start+int(rawLen) { return XValue{} }
	raw := make([]byte, rawLen)
	copy(raw, data[start:start+int(rawLen)])
	return XValue{kind: kind, raw: raw}
}

func EncodedXSize(v XValue) int {
	if v.IsNil() { return 0 }
	return 1 + len(v.kind) + 4 + len(v.raw)
}

func isValidKind(s string) bool {
	if len(s) == 0 || len(s) > 127 { return false }
	for _, c := range s {
		if !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_') {
			return false
		}
	}
	return true
}
