package main

import (
	"fmt"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"strconv"
)

func incrVtid(kv kvspace.KVSpace) string {
	valV, _ := kv.Get(keytree.VthreadSeq)
	n, _ := strconv.ParseInt(valV.Str(), 10, 64)
	n++
	kv.Set(keytree.VthreadSeq, kvspace.Str(strconv.FormatInt(n, 10)))
	return fmt.Sprintf("%d", n)
}

