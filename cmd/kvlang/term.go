package main

import (
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

func registerDefaultTerm(kv kvspace.KVSpace) {
	kv.Set(keytree.SysTerm("kvlangrun", "stdout")+"/type", "file")
	kv.Set(keytree.SysTerm("kvlangrun", "stdout")+"/detail", "/dev/stdout")
	kv.Set(keytree.SysTerm("kvlangrun", "stderr")+"/type", "file")
	kv.Set(keytree.SysTerm("kvlangrun", "stderr")+"/detail", "/dev/stderr")
	kv.Set(keytree.SysTerm("kvlangrun", "stdin")+"/type", "file")
	kv.Set(keytree.SysTerm("kvlangrun", "stdin")+"/detail", "/dev/stdin")
}

