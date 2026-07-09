package main

import (
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

func registerDefaultTerm(kv kvspace.KVSpace) {
	kv.Set(keytree.SysTerm("kvlangrun", "stdout")+"/type", "file", 0)
	kv.Set(keytree.SysTerm("kvlangrun", "stdout")+"/detail", "/dev/stdout", 0)
	kv.Set(keytree.SysTerm("kvlangrun", "stderr")+"/type", "file", 0)
	kv.Set(keytree.SysTerm("kvlangrun", "stderr")+"/detail", "/dev/stderr", 0)
	kv.Set(keytree.SysTerm("kvlangrun", "stdin")+"/type", "file", 0)
	kv.Set(keytree.SysTerm("kvlangrun", "stdin")+"/detail", "/dev/stdin", 0)
}

