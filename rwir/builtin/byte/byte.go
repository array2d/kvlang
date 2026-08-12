// Package byte provides base methods for all byte-derived kinds (bool, int*, uint*, float*, stringbyte).
package byte

import "github.com/array2d/kvspace-go"

func mustByte(v kvspace.XValue) {
	if !kvspace.IsByteDerived(v.Kind()) {
		panic("byte: expected byte-derived kind, got " + v.Kind())
	}
}

// Len returns ArrayLen() for any byte-derived kind.
func Len(v kvspace.XValue) int {
	mustByte(v)
	return int(v.ArrayLen())
}

// At returns the i-th element for any byte-derived kind.
func At(v kvspace.XValue, i int) kvspace.XValue {
	mustByte(v)
	raw := v.Encode()[1+len(v.Kind())+1+4+4:]
	return kvspace.SliceElem(v.Kind(), raw, int32(i))
}

// Set returns a copy with the i-th element replaced (any byte-derived kind).
func Set(v kvspace.XValue, i int, val kvspace.XValue) kvspace.XValue {
	mustByte(v)
	if kvspace.ElemSize(v.Kind()) <= 0 {
		panic("byte.Set: unsupported kind " + v.Kind())
	}
	raw := v.Encode()
	head := raw[:1+len(v.Kind())+1+4+4]
	body := make([]byte, len(raw)-len(head))
	copy(body, raw[len(head):])
	kvspace.WriteElem(v.Kind(), body, int32(i), val)
	return kvspace.DecodeXValueHead(append(append(head[:1], []byte(v.Kind())...), raw[1+len(v.Kind()):]...)).Decode()
	// re-encode via EncodeRaw
	n := len(body) / int(kvspace.ElemSize(v.Kind()))
	buf := kvspace.EncodeRaw(v.Kind(), false, body, int32(n))
	return kvspace.DecodeXValueHead(buf).Decode()
}

// Slice returns a sub-slice [lo:hi] for any byte-derived kind.
func Slice(v kvspace.XValue, lo, hi int) kvspace.XValue {
	mustByte(v)
	al := int(v.ArrayLen())
	if lo < 0 { lo = 0 }
	if hi < 0 || hi > al { hi = al }
	if lo >= hi { lo = hi }
	n := hi - lo
	if n <= 0 {
		return kvspace.SliceElem(v.Kind(), nil, 0)
	}
	raw := v.Encode()[1+len(v.Kind())+1+4+4:]
	es := int(kvspace.ElemSize(v.Kind()))
	return kvspace.DecodeXValueHead(
		kvspace.EncodeRaw(v.Kind(), false, raw[lo*es:hi*es], int32(n)),
	).Decode()
}
