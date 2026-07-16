package kvspace

import (
	"encoding/binary"
	"math"
	"strconv"
)

// Value 是 kvspace 中存储的类型化值。
//   - 零值（IsNil()==true）表示"不存在"或"未初始化"。
//   - 一旦由构造函数创建，字段不可修改（逻辑不可变）。
//   - raw 字节由 Value 自身 owned，不与外部缓冲区共享。
type XValue struct {
	kind string // vtype name
	raw  []byte // 类型化原始字节
}

// ── 构造函数 ─────────────────────────────────────────────────────────────────
// kind 名称与 vtype.Kind* 常量对齐；因循环导入限制，此处直接使用字符串字面量。

func Int(v int64) XValue     { return XValue{kind: "int", raw: encodeInt64(v)} }
func Float(v float64) XValue { return XValue{kind: "float", raw: encodeFloat64(v)} }
func Bool(v bool) XValue {
	b := byte(0)
	if v {
		b = 1
	}
	return XValue{kind: "bool", raw: []byte{b}}
}
func Str(v string) XValue   { return XValue{kind: "string", raw: []byte(v)} }
func Bytes(v []byte) XValue { c := make([]byte, len(v)); copy(c, v); return XValue{kind: "bytes", raw: c} }

// Raw 构造任意 vtype 的 Value（用于第三方 vtype 扩展，如 "tensor"）。
// raw 会被复制，调用方可安全复用原缓冲区。
func Raw(kind string, raw []byte) XValue {
	c := make([]byte, len(raw))
	copy(c, raw)
	return XValue{kind: kind, raw: c}
}

// ── 判断 ─────────────────────────────────────────────────────────────────────

func (v XValue) IsNil() bool  { return v.kind == "" }
func (v XValue) Kind() string { return v.kind }

// ── 访问器（类型不匹配时返回零值，不 panic）────────────────────────────────

func (v XValue) Int() int64 {
	if v.kind != "int" || len(v.raw) < 8 {
		return 0
	}
	return decodeInt64(v.raw)
}

func (v XValue) Float() float64 {
	if v.kind != "float" || len(v.raw) < 8 {
		return 0
	}
	return decodeFloat64(v.raw)
}

func (v XValue) Bool() bool {
	if v.kind != "bool" || len(v.raw) == 0 {
		return false
	}
	return v.raw[0] != 0
}

// Str 返回字符串内容。Kind() != "string" 时返回 ""。
// 注意：与 String()（fmt.Stringer 调试格式）不同。
func (v XValue) Str() string {
	if v.kind != "string" {
		return ""
	}
	return string(v.raw)
}

func (v XValue) Bytes() []byte {
	if v.kind != "bytes" {
		return nil
	}
	return v.raw
}

// RawBytes 返回底层原始字节（任意 kind）。不拷贝，调用方不得修改。
func (v XValue) RawBytes() []byte { return v.raw }

// ── Stringer ─────────────────────────────────────────────────────────────────

// String 实现 fmt.Stringer，输出调试格式 "kind:repr"，例如：
//
//	int:42    float:3.14    bool:true    str:hello    nil    tensor:120B
//
// 获取 str 类型的字符串内容请用 v.Str()，不要用 v.String()。
func (v XValue) String() string {
	switch v.kind {
	case "":
		return "nil"
	case "int":
		return "int:" + strconv.FormatInt(v.Int(), 10)
	case "float":
		return "float:" + strconv.FormatFloat(v.Float(), 'f', -1, 64)
	case "bool":
		return "bool:" + strconv.FormatBool(v.Bool())
	case "string":
		return "string:" + v.Str()
	default:
		return v.kind + ":" + strconv.Itoa(len(v.raw)) + "B"
	}
}

// ── TLV 编解码 ────────────────────────────────────────────────────────────────
//
// 格式：[1B kind_len][N B kind_name][4B raw_len LE][M B raw_value]
// IsNil() 的 Value 编码为 nil（零字节）。

// EncodeXValue 将 Value 编码为完全自描述的 TLV 字节。
func EncodeXValue(v XValue) []byte {
	if v.IsNil() {
		return nil
	}
	buf := make([]byte, 1+len(v.kind)+4+len(v.raw))
	buf[0] = byte(len(v.kind))
	copy(buf[1:], v.kind)
	binary.LittleEndian.PutUint32(buf[1+len(v.kind):], uint32(len(v.raw)))
	copy(buf[1+len(v.kind)+4:], v.raw)
	return buf
}

// DecodeXValue 从 TLV 字节解码为 Value。
// raw 字节在内部复制，返回的 Value 不与 data 共享内存。
// 格式不合法（截断、kind 非法、长度溢出）时返回零值 Value{}。
func DecodeXValue(data []byte) XValue {
	if len(data) == 0 {
		return XValue{}
	}
	kindLen := int(data[0])
	if len(data) < 1+kindLen+4 {
		return XValue{}
	}
	kind := string(data[1 : 1+kindLen])
	if !isValidKind(kind) {
		return XValue{}
	}
	rawLen := binary.LittleEndian.Uint32(data[1+kindLen : 1+kindLen+4])
	start := 1 + kindLen + 4
	if len(data) < start+int(rawLen) {
		return XValue{}
	}
	raw := make([]byte, rawLen)
	copy(raw, data[start:start+int(rawLen)])
	return XValue{kind: kind, raw: raw}
}

// EncodedXSize 返回 Value 编码后的字节数（用于缓冲区容量预估）。
func EncodedXSize(v XValue) int {
	if v.IsNil() {
		return 0
	}
	return 1 + len(v.kind) + 4 + len(v.raw)
}

// isValidKind 检查 kind name 是否合法（[a-zA-Z0-9_]，非空，长度 ≤ 127）。
func isValidKind(s string) bool {
	if len(s) == 0 || len(s) > 127 {
		return false
	}
	for _, c := range s {
		if !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_') {
			return false
		}
	}
	return true
}

// ── 数值编解码（小端，8 字节）────────────────────────────────────────────────

func encodeInt64(v int64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, uint64(v))
	return b
}

func decodeInt64(b []byte) int64 {
	return int64(binary.LittleEndian.Uint64(b))
}

func encodeFloat64(v float64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, math.Float64bits(v))
	return b
}

func decodeFloat64(b []byte) float64 {
	return math.Float64frombits(binary.LittleEndian.Uint64(b))
}
