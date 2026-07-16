package kvspace

// ── 字符串 ────────────────────────────────────────────────────────────────

func Str(v string) XValue { return XValue{kind: "string", raw: []byte(v)} }

func (v XValue) Str() string {
	if v.kind != "string" { return "" }
	return string(v.raw)
}

// ── 字节数组 ──────────────────────────────────────────────────────────────

func Bytes(v []byte) XValue {
	c := make([]byte, len(v))
	copy(c, v)
	return XValue{kind: "bytes", raw: c}
}

func (v XValue) Bytes() []byte {
	if v.kind != "bytes" { return nil }
	return v.raw
}
