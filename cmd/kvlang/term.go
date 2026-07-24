package main

import (
	"kvlang/internal/keytree"
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
		{h + "stdout/type", kvspace.Str("file")},
		{h + "stdout/detail", kvspace.Str("/dev/stdout")},
		{h + "stderr/type", kvspace.Str("file")},
		{h + "stderr/detail", kvspace.Str("/dev/stderr")},
		{h + "stdin/type", kvspace.Str("file")},
		{h + "stdin/detail", kvspace.Str("/dev/stdin")},
	})
}
