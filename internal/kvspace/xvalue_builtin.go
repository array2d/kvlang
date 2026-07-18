package kvspace

import (
	"encoding/binary"
	"math"
)

// ── 整型构造函数 ─────────────────────────────────────────────────────────
// 小端编码，固定宽度

func Int8(v int8) XValue   { return XValue{kind: "int8", raw: encodeInt8(v)} }
func Int16(v int16) XValue  { return XValue{kind: "int16", raw: encodeInt16(v)} }
func Int32(v int32) XValue  { return XValue{kind: "int32", raw: encodeInt32(v)} }
func Int64(v int64) XValue  { return XValue{kind: "int64", raw: encodeInt64(v)} }
func Int(v int64) XValue    { return Int64(v) } // alias

// ── 无符号整型 ────────────────────────────────────────────────────────────

func Uint8(v uint8) XValue   { return XValue{kind: "uint8", raw: encodeUint8(v)} }
func Uint16(v uint16) XValue  { return XValue{kind: "uint16", raw: encodeUint16(v)} }
func Uint32(v uint32) XValue  { return XValue{kind: "uint32", raw: encodeUint32(v)} }
func Uint64(v uint64) XValue  { return XValue{kind: "uint64", raw: encodeUint64(v)} }

// ── 浮点 ──────────────────────────────────────────────────────────────────

func Float32(v float32) XValue { return XValue{kind: "float32", raw: encodeFloat32(v)} }
func Float64(v float64) XValue { return XValue{kind: "float64", raw: encodeFloat64(v)} }
func Float(v float64) XValue   { return Float64(v) } // alias

// ── 布尔 ──────────────────────────────────────────────────────────────────

func Bool(v bool) XValue {
	b := byte(0)
	if v { b = 1 }
	return XValue{kind: "bool", raw: []byte{b}}
}

// Dict 返回 dict 类型标记值：写在键族 base 键上，成员是 base.名 平坦键（键族本身无容器）。
// 非 string 值 → 成员解析走按名回退（deep-dive §10.4），成员键 = 帧感知(base).名。
func Dict() XValue { return XValue{kind: "dict"} }

// ── 整型访问器 ────────────────────────────────────────────────────────────

func (v XValue) Int8() int8 {
	if v.kind != "int8" || len(v.raw) < 1 { return 0 }
	return int8(v.raw[0])
}
func (v XValue) Int16() int16 {
	if v.kind != "int16" || len(v.raw) < 2 { return 0 }
	return int16(binary.LittleEndian.Uint16(v.raw))
}
func (v XValue) Int32() int32 {
	if v.kind != "int32" || len(v.raw) < 4 { return 0 }
	return int32(binary.LittleEndian.Uint32(v.raw))
}
func (v XValue) Int64() int64 {
	if v.kind != "int64" || len(v.raw) < 8 { return 0 }
	return int64(binary.LittleEndian.Uint64(v.raw))
}
func (v XValue) Int() int64 { return v.Int64() } // alias

// ── 无符号整型访问器 ──────────────────────────────────────────────────────

func (v XValue) Uint8() uint8 {
	if v.kind != "uint8" || len(v.raw) < 1 { return 0 }
	return v.raw[0]
}
func (v XValue) Uint16() uint16 {
	if v.kind != "uint16" || len(v.raw) < 2 { return 0 }
	return binary.LittleEndian.Uint16(v.raw)
}
func (v XValue) Uint32() uint32 {
	if v.kind != "uint32" || len(v.raw) < 4 { return 0 }
	return binary.LittleEndian.Uint32(v.raw)
}
func (v XValue) Uint64() uint64 {
	if v.kind != "uint64" || len(v.raw) < 8 { return 0 }
	return binary.LittleEndian.Uint64(v.raw)
}

// ── 浮点访问器 ────────────────────────────────────────────────────────────

func (v XValue) Float32() float32 {
	if v.kind != "float32" || len(v.raw) < 4 { return 0 }
	return math.Float32frombits(binary.LittleEndian.Uint32(v.raw))
}
func (v XValue) Float64() float64 {
	if v.kind != "float64" || len(v.raw) < 8 { return 0 }
	return math.Float64frombits(binary.LittleEndian.Uint64(v.raw))
}
func (v XValue) Float() float64 { return v.Float64() } // alias

// ── 布尔访问器 ────────────────────────────────────────────────────────────

func (v XValue) Bool() bool {
	if v.kind != "bool" || len(v.raw) == 0 { return false }
	return v.raw[0] != 0
}

// ── 编码 helpers ──────────────────────────────────────────────────────────

func encodeInt8(v int8) []byte     { return []byte{byte(v)} }
func encodeInt16(v int16) []byte   { b := make([]byte, 2); binary.LittleEndian.PutUint16(b, uint16(v)); return b }
func encodeInt32(v int32) []byte   { b := make([]byte, 4); binary.LittleEndian.PutUint32(b, uint32(v)); return b }
func encodeInt64(v int64) []byte   { b := make([]byte, 8); binary.LittleEndian.PutUint64(b, uint64(v)); return b }
func encodeUint8(v uint8) []byte   { return []byte{v} }
func encodeUint16(v uint16) []byte { b := make([]byte, 2); binary.LittleEndian.PutUint16(b, v); return b }
func encodeUint32(v uint32) []byte { b := make([]byte, 4); binary.LittleEndian.PutUint32(b, v); return b }
func encodeUint64(v uint64) []byte { b := make([]byte, 8); binary.LittleEndian.PutUint64(b, v); return b }
func encodeFloat32(v float32) []byte { b := make([]byte, 4); binary.LittleEndian.PutUint32(b, math.Float32bits(v)); return b }
func encodeFloat64(v float64) []byte { b := make([]byte, 8); binary.LittleEndian.PutUint64(b, math.Float64bits(v)); return b }
