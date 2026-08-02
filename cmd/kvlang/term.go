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
		{h + "stdout/type", kvspace.NewChar("file")},
		{h + "stdout/detail", kvspace.NewChar("/dev/stdout")},
		{h + "stderr/type", kvspace.NewChar("file")},
		{h + "stderr/detail", kvspace.NewChar("/dev/stderr")},
		{h + "stdin/type", kvspace.NewChar("file")},
		{h + "stdin/detail", kvspace.NewChar("/dev/stdin")},
	})
}
