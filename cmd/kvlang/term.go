package main

import (
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

func registerDefaultTerm(kv kvspace.KVSpace) {
	kv.Set(keytree.DevTTY("kvlangrun", "stdout")+"/type", "file")
	kv.Set(keytree.DevTTY("kvlangrun", "stdout")+"/detail", "/dev/stdout")
	kv.Set(keytree.DevTTY("kvlangrun", "stderr")+"/type", "file")
	kv.Set(keytree.DevTTY("kvlangrun", "stderr")+"/detail", "/dev/stderr")
	kv.Set(keytree.DevTTY("kvlangrun", "stdin")+"/type", "file")
	kv.Set(keytree.DevTTY("kvlangrun", "stdin")+"/detail", "/dev/stdin")
}

