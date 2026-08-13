package main

import (
	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
)

func initDirs(kv kvspace.KVSpace) {
	kvspace.MkIndexRecursive(kv, "/lib/")
	kvspace.MkIndexRecursive(kv, "/vthread/")
}

func registerDefaultTerm(kv kvspace.KVSpace) {
	initDirs(kv)
	h := keytree.DevTTY("kvlangrun", "")
	kvspace.MkIndexRecursive(kv, h+"stdout/")
	kvspace.MkIndexRecursive(kv, h+"stderr/")
	kvspace.MkIndexRecursive(kv, h+"stdin/")
	kv.Set([]kvspace.KVPair{
		{Key: h + "stdout/type", Val: kvspace.NewStringByte([]byte("file")...)},
		{Key: h + "stdout/detail", Val: kvspace.NewStringByte([]byte("/dev/stdout")...)},
		{Key: h + "stderr/type", Val: kvspace.NewStringByte([]byte("file")...)},
		{Key: h + "stderr/detail", Val: kvspace.NewStringByte([]byte("/dev/stderr")...)},
		{Key: h + "stdin/type", Val: kvspace.NewStringByte([]byte("file")...)},
		{Key: h + "stdin/detail", Val: kvspace.NewStringByte([]byte("/dev/stdin")...)},
	})
}
