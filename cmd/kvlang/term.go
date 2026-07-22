package main

import (
	"kvlang/internal/keytree"
	"github.com/array2d/kvspace-go"
)

func registerDefaultTerm(kv kvspace.KVSpace) {
	kv.Set([]kvspace.KVPair{
		{keytree.DevTTY("kvlangrun", "stdout") + "/type", kvspace.Str("file")},
		{keytree.DevTTY("kvlangrun", "stdout") + "/detail", kvspace.Str("/dev/stdout")},
		{keytree.DevTTY("kvlangrun", "stderr") + "/type", kvspace.Str("file")},
		{keytree.DevTTY("kvlangrun", "stderr") + "/detail", kvspace.Str("/dev/stderr")},
		{keytree.DevTTY("kvlangrun", "stdin") + "/type", kvspace.Str("file")},
		{keytree.DevTTY("kvlangrun", "stdin") + "/detail", kvspace.Str("/dev/stdin")},
	})
}

