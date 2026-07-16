package kvspace

import (
	"encoding/binary"
	"strconv"
	"strings"
)

// ── 1D 数组 ──────────────────────────────────────────────────────────────
//
// XValue{kind: "array"} 存储同类型元素的线性序列。
// raw 格式：[4B count LE][elem0 TLV][elem1 TLV]...
// 每个元素独立 EncodeXValue，保留自身类型。

const KindArray = "array"

// Array 构造 1D 数组。元素可以是任意 XValue（类型可混合）。
func Array(elems []XValue) XValue {
	total := 4 // count header
	encoded := make([][]byte, len(elems))
	for i, e := range elems {
		encoded[i] = EncodeXValue(e)
		total += len(encoded[i])
	}
	raw := make([]byte, total)
	binary.LittleEndian.PutUint32(raw[:4], uint32(len(elems)))
	off := 4
	for _, enc := range encoded {
		copy(raw[off:], enc)
		off += len(enc)
	}
	return XValue{kind: KindArray, raw: raw}
}

// ArrayInts 构造整数 1D 数组的便捷方法。
func ArrayInts(vals []int64) XValue {
	elems := make([]XValue, len(vals))
	for i, v := range vals {
		elems[i] = Int64(v)
	}
	return Array(elems)
}

// Len 返回数组元素个数。非数组类型返回 0。
func (v XValue) Len() int {
	if v.kind != KindArray || len(v.raw) < 4 {
		return 0
	}
	return int(binary.LittleEndian.Uint32(v.raw[:4]))
}

// Index 返回数组的第 i 个元素。越界返回 nil。
func (v XValue) Index(i int) XValue {
	n := v.Len()
	if i < 0 || i >= n {
		return XValue{}
	}
	off := 4
	for j := 0; j < i; j++ {
		_, size := decodeXValueLen(v.raw[off:])
		off += size
	}
	elem, _ := decodeXValueLen(v.raw[off:])
	return DecodeXValue(v.raw[off : off+elem])
}

// decodeXValueLen 返回 (总字节数, 消耗的 raw 字节数)。不分配内存。
func decodeXValueLen(data []byte) (total, consumed int) {
	if len(data) == 0 {
		return 0, 1
	}
	kindLen := int(data[0])
	if len(data) < 1+kindLen+4 {
		return 0, len(data)
	}
	rawLen := int(binary.LittleEndian.Uint32(data[1+kindLen:]))
	consumed = 1 + kindLen + 4 + rawLen
	return consumed, consumed
}

// ── ND 数组（高维） ──────────────────────────────────────────────────────
//
// 高维数组主体存为 1D 数组（逐元素 EncodeXValue），
// 维度信息存于相邻 key {path}/shape，值为 1D 整数数组。

// Shape 返回数组的形状（从 {path}/shape key 读取）。
// 1D 数组：返回 [len]。
// ND 数组：需要调用方传入从 KV 读取的 shape XValue。
func (v XValue) Shape(shapeVal XValue) []int {
	n := v.Len()
	if n == 0 {
		return nil
	}
	if shapeVal.IsNil() {
		return []int{n} // 无 shape → 视为 1D
	}
	if shapeVal.kind != KindArray {
		return []int{n} // shape 不是数组 → 回退
	}
	dims := make([]int, shapeVal.Len())
	for i := range dims {
		dims[i] = int(shapeVal.Index(i).Int64())
	}
	return dims
}

// Stride 从 shape 计算 row-major stride。
// shape=[d0,d1,d2] → stride=[d1*d2, d2, 1]
func Stride(shape []int) []int {
	if len(shape) == 0 {
		return nil
	}
	s := make([]int, len(shape))
	s[len(shape)-1] = 1
	for i := len(shape) - 2; i >= 0; i-- {
		s[i] = s[i+1] * shape[i+1]
	}
	return s
}

// FlatIndex 将多维索引 [i0,i1,...,in-1] 转为平坦索引。
// flat = sum(indices[k] * stride[k])
func FlatIndex(indices, stride []int) int {
	if len(indices) != len(stride) {
		return -1
	}
	f := 0
	for i := range indices {
		f += indices[i] * stride[i]
	}
	return f
}

// ShapeKey 返回 array path 对应的 shape 子 key。
func ShapeKey(path string) string { return path + "/shape" }

// ShapeString 将 shape 转为逗号分隔字符串（如 "2,3"）。
func ShapeString(shape []int) string {
	parts := make([]string, len(shape))
	for i, d := range shape {
		parts[i] = strconv.Itoa(d)
	}
	return strings.Join(parts, ",")
}

// ParseShape 从逗号分隔字符串解析 shape。
func ParseShape(s string) []int {
	if s == "" { return nil }
	parts := strings.Split(s, ",")
	dims := make([]int, len(parts))
	for i, p := range parts {
		d, _ := strconv.Atoi(strings.TrimSpace(p))
		dims[i] = d
	}
	return dims
}

// ── 高维访问 ─────────────────────────────────────────────────────────────
//
// a[i, j, k] → At([]int{i, j, k}, stride) 返回 XValue
// stride 由 Shape + Stride 预计算，调用方缓存避免重复计算。

// At 用多维索引 + stride 读取数组元素。
// indices 长度必须等于 stride 长度。
func (v XValue) At(indices, stride []int) XValue {
	f := FlatIndex(indices, stride)
	if f < 0 {
		return XValue{}
	}
	return v.Index(f)
}
