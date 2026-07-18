package main

import (
	"kvlang/internal/keytree"
	"github.com/array2d/kvlang-go"
)

func registerDefaultTerm(kv kvspace.KVSpace) {
	kv.Set(keytree.DevTTY("kvlangrun", "stdout")+"/type", kvspace.Str("file"))
	kv.Set(keytree.DevTTY("kvlangrun", "stdout")+"/detail", kvspace.Str("/dev/stdout"))
	kv.Set(keytree.DevTTY("kvlangrun", "stderr")+"/type", kvspace.Str("file"))
	kv.Set(keytree.DevTTY("kvlangrun", "stderr")+"/detail", kvspace.Str("/dev/stderr"))
	kv.Set(keytree.DevTTY("kvlangrun", "stdin")+"/type", kvspace.Str("file"))
	kv.Set(keytree.DevTTY("kvlangrun", "stdin")+"/detail", kvspace.Str("/dev/stdin"))
}

