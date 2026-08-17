// Package utf8 provides UTF-8 encoding/decoding operations on uint8 arrays (strings).
package utf8

import (
	"unicode/utf8"

	"github.com/array2d/kvspace-go"
)

// Bytes returns the raw UTF-8 bytes of a char/utf8 XValue.
func Bytes(v kvspace.XValue) []byte {
	if v.Kind() != kvspace.KindCharUtf8 {
		panic("utf8.Bytes: expected char/utf8, got " + v.Kind())
	}
	return kvspace.BodyBytes(v)
}

// String decodes UTF-8 bytes to a Go string.
func String(v kvspace.XValue) string {
	return string(Bytes(v))
}

// Len returns the number of UTF-8 runes in the charbyte array.
func Len(v kvspace.XValue) int {
	return utf8.RuneCount(Bytes(v))
}

// At returns the i-th rune as a charbyte (UTF-8 encoding of a single char).
func At(v kvspace.XValue, i int) kvspace.XValue {
	b := Bytes(v)
	var idx int
	for r := 0; r < i && idx < len(b); r++ {
		_, sz := utf8.DecodeRune(b[idx:])
		idx += sz
	}
	if idx >= len(b) {
		return kvspace.NewCharByte([]byte("")...)
	}
	r, sz := utf8.DecodeRune(b[idx:])
	return kvspace.NewCharByte([]byte(string(runeToUTF8(r, sz)))...)
}

// Set returns a new charbyte with the i-th rune replaced.
func Set(v kvspace.XValue, i int, ch kvspace.XValue) kvspace.XValue {
	b := Bytes(v)
	chb := Bytes(ch)
	var idx int
	for r := 0; r < i && idx < len(b); r++ {
		_, sz := utf8.DecodeRune(b[idx:])
		idx += sz
	}
	if idx >= len(b) {
		return v
	}
	_, sz := utf8.DecodeRune(b[idx:])
	result := make([]byte, 0, len(b)-sz+len(chb))
	result = append(result, b[:idx]...)
	result = append(result, chb...)
	result = append(result, b[idx+sz:]...)
	return kvspace.NewCharByte(result...)
}

// Slice returns a substring from lo to hi (rune indices).
func Slice(v kvspace.XValue, lo, hi int) kvspace.XValue {
	b := Bytes(v)
	runeStarts := runeOffsets(b)
	if lo < 0 || lo > len(runeStarts) {
		return kvspace.NewCharByte([]byte("")...)
	}
	if hi > len(runeStarts) {
		hi = len(runeStarts)
	}
	if lo >= hi {
		return kvspace.NewCharByte([]byte("")...)
	}
	start := runeStarts[lo]
	end := start
	if hi < len(runeStarts) {
		end = runeStarts[hi]
	} else {
		end = len(b)
	}
	return kvspace.NewCharByte(b[start:end]...)
}

// runeOffsets returns byte offsets of each rune start.
func runeOffsets(b []byte) []int {
	var offsets []int
	for i := 0; i < len(b); {
		offsets = append(offsets, i)
		_, sz := utf8.DecodeRune(b[i:])
		i += sz
	}
	return offsets
}

func runeToUTF8(r rune, sz int) []byte {
	buf := make([]byte, sz)
	utf8.EncodeRune(buf, r)
	return buf
}

// String returns the raw bytes of a uint8 array decoded as a Go string.
