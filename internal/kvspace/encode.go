package kvspace

import "encoding/binary"

// EncodeValue 将 Value 编码为完全自描述的 TLV 字节。
// 格式：[1B kind_len][N B kind_name][4B raw_len LE][M B raw_value]
// IsNil() 的 Value 返回 nil。
func EncodeValue(v Value) []byte {
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

// DecodeValue 从 TLV 字节解码为 Value。
// raw 字节在内部复制，返回的 Value 不与 data 共享内存。
// 格式不合法（截断、kind 非法、长度溢出）时返回零值 Value{}。
func DecodeValue(data []byte) Value {
	if len(data) == 0 {
		return Value{}
	}
	kindLen := int(data[0])
	if len(data) < 1+kindLen+4 {
		return Value{}
	}
	kind := string(data[1 : 1+kindLen])
	if !isValidKind(kind) {
		return Value{}
	}
	rawLen := binary.LittleEndian.Uint32(data[1+kindLen : 1+kindLen+4])
	start := 1 + kindLen + 4
	if len(data) < start+int(rawLen) {
		return Value{}
	}
	raw := make([]byte, rawLen)
	copy(raw, data[start:start+int(rawLen)])
	return Value{kind: kind, raw: raw}
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

// EncodedSize 返回 Value 编码后的字节数（用于缓冲区容量预估）。
func EncodedSize(v Value) int {
	if v.IsNil() {
		return 0
	}
	return 1 + len(v.kind) + 4 + len(v.raw)
}

// ── 小端整数编解码 ────────────────────────────────────────────────────────────

func encodeInt64LE(v int64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, uint64(v))
	return b
}

func decodeInt64LE(b []byte) int64 {
	return int64(binary.LittleEndian.Uint64(b))
}

func encodeFloat64LE(v float64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, mathFloat64bits(v))
	return b
}

func decodeFloat64LE(b []byte) float64 {
	return mathFloat64frombits(binary.LittleEndian.Uint64(b))
}
