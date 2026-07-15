package kvspace

import "strconv"

// Value 是 kvspace 中存储的类型化值。
//   - 零值（IsNil()==true）表示"不存在"或"未初始化"。
//   - 一旦由构造函数创建，字段不可修改（逻辑不可变）。
//   - raw 字节由 Value 自身 owned，不与外部缓冲区共享。
type Value struct {
	kind string // vtype name
	raw  []byte // 类型化原始字节
}

// ── 构造函数 ─────────────────────────────────────────────────────────────────
// kind 名称与 vtype.Kind* 常量对齐；因循环导入限制，此处直接使用字符串字面量。

func Int(v int64) Value     { return Value{kind: "int", raw: encodeInt64LE(v)} }
func Float(v float64) Value { return Value{kind: "float", raw: encodeFloat64LE(v)} }
func Bool(v bool) Value {
	b := byte(0)
	if v {
		b = 1
	}
	return Value{kind: "bool", raw: []byte{b}}
}
func Str(v string) Value   { return Value{kind: "str", raw: []byte(v)} }
func Bytes(v []byte) Value { c := make([]byte, len(v)); copy(c, v); return Value{kind: "bytes", raw: c} }

// Raw 构造任意 vtype 的 Value（用于第三方 vtype 扩展，如 "tensor"）。
// raw 会被复制，调用方可安全复用原缓冲区。
func Raw(kind string, raw []byte) Value {
	c := make([]byte, len(raw))
	copy(c, raw)
	return Value{kind: kind, raw: c}
}

// ── 判断 ─────────────────────────────────────────────────────────────────────

func (v Value) IsNil() bool  { return v.kind == "" }
func (v Value) Kind() string { return v.kind }

// ── 访问器（类型不匹配时返回零值，不 panic）────────────────────────────────

func (v Value) Int() int64 {
	if v.kind != "int" || len(v.raw) < 8 {
		return 0
	}
	return decodeInt64LE(v.raw)
}

func (v Value) Float() float64 {
	if v.kind != "float" || len(v.raw) < 8 {
		return 0
	}
	return decodeFloat64LE(v.raw)
}

func (v Value) Bool() bool {
	if v.kind != "bool" || len(v.raw) == 0 {
		return false
	}
	return v.raw[0] != 0
}

// Str 返回字符串内容。Kind() != "str" 时返回 ""。
// 注意：与 String()（fmt.Stringer 调试格式）不同。
func (v Value) Str() string {
	if v.kind != "str" {
		return ""
	}
	return string(v.raw)
}

func (v Value) Bytes() []byte {
	if v.kind != "bytes" {
		return nil
	}
	return v.raw
}

// RawBytes 返回底层原始字节（任意 kind）。不拷贝，调用方不得修改。
func (v Value) RawBytes() []byte { return v.raw }

// ── Stringer ─────────────────────────────────────────────────────────────────

// String 实现 fmt.Stringer，输出调试格式 "kind:repr"，例如：
//
//	int:42    float:3.14    bool:true    str:hello    nil    tensor:120B
//
// 获取 str 类型的字符串内容请用 v.Str()，不要用 v.String()。
func (v Value) String() string {
	switch v.kind {
	case "":
		return "nil"
	case "int":
		return "int:" + strconv.FormatInt(v.Int(), 10)
	case "float":
		return "float:" + strconv.FormatFloat(v.Float(), 'f', -1, 64)
	case "bool":
		return "bool:" + strconv.FormatBool(v.Bool())
	case "str":
		return "str:" + v.Str()
	default:
		return v.kind + ":" + strconv.Itoa(len(v.raw)) + "B"
	}
}
