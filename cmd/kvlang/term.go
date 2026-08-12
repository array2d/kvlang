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
		{h + "stdout/type", kvspace.NewChar("file"), -1},
		{h + "stdout/detail", kvspace.NewChar("/dev/stdout"), -1},
		{h + "stderr/type", kvspace.NewChar("file"), -1},
		{h + "stderr/detail", kvspace.NewChar("/dev/stderr"), -1},
		{h + "stdin/type", kvspace.NewChar("file"), -1},
		{h + "stdin/detail", kvspace.NewChar("/dev/stdin"), -1},
	})
}
