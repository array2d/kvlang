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
		{h + "stdout/type", kvspace.String("file")},
		{h + "stdout/detail", kvspace.String("/dev/stdout")},
		{h + "stderr/type", kvspace.String("file")},
		{h + "stderr/detail", kvspace.String("/dev/stderr")},
		{h + "stdin/type", kvspace.String("file")},
		{h + "stdin/detail", kvspace.String("/dev/stdin")},
	})
}
