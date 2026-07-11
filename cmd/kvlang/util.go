package main

import (
	"fmt"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"strconv"
)

func incrVtid(kv kvspace.KVSpace) string {
	val, _ := kv.Get(keytree.SysVtidCounter)
	n, _ := strconv.ParseInt(val, 10, 64)
	n++
	kv.Set(keytree.SysVtidCounter, strconv.FormatInt(n, 10))
	return fmt.Sprintf("%d", n)
}

