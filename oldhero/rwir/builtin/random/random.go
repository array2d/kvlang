// Package random provides uint64 random number generation.
package random

import (
	"crypto/rand"
	"encoding/binary"

	"github.com/array2d/kvspace-go"
)

// Uint64 returns a cryptographically random uint64.
func Uint64() kvspace.XValue {
	var buf [8]byte
	rand.Read(buf[:])
	return kvspace.NewUint64(binary.LittleEndian.Uint64(buf[:]))
}

// Int63 returns a non-negative int64 (≤ 2⁶³-1).
func Int63() kvspace.XValue {
	var buf [8]byte
	rand.Read(buf[:])
	return kvspace.NewInt64(int64(binary.LittleEndian.Uint64(buf[:]) >> 1))
}

// Intn returns uint64 in [0, n).
func Intn(n uint64) kvspace.XValue {
	if n == 0 {
		return kvspace.NewUint64(0)
	}
	var buf [8]byte
	rand.Read(buf[:])
	return kvspace.NewUint64(binary.LittleEndian.Uint64(buf[:]) % n)
}
