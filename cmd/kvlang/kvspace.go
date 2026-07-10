package main

import (
	"fmt"
	"os"

	"kvlang/internal/kvspace"
	"kvlang/internal/keytree"
)

func cmdKVSpace(args []string) {
	if len(args) < 1 {
		fmt.Fprintln(os.Stderr, "usage: kvlang kvspace <get|set|del|list|watch> [args]")
		os.Exit(1)
	}

	kv := kvspace.Conn("127.0.0.1:6379")
	defer kv.DisConn()

	switch args[0] {
	case "get":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace get <key>"); os.Exit(1) }
		val, err := kv.Get(args[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		fmt.Println(val)

	case "set":
		if len(args) < 3 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace set <key> <value>"); os.Exit(1) }
		kv.Set(args[1], args[2], 0)

	case "del":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace del <key>"); os.Exit(1) }
		kv.Del(args[1])

	case "list":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace list <prefix>"); os.Exit(1) }
		children, err := kv.List(args[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for _, c := range children { fmt.Println(c) }

	case "watch":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace watch <key>"); os.Exit(1) }
		results, err := kv.Watch(0, args[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for _, r := range results { fmt.Println(r) }

	case "clear":
		clearAll(kv)

	default:
		fmt.Fprintf(os.Stderr, "unknown kvspace subcommand: %s\n", args[0])
		os.Exit(1)
	}
}

func clearAll(kv kvspace.KVSpace) {
	clearRoot(kv, keytree.VthreadRoot)
	clearRoot(kv, keytree.SrcRoot)
	clearRoot(kv, keytree.FuncRoot)
	clearRoot(kv, keytree.SysRoot)
	clearRoot(kv, keytree.OpRoot)
	kv.Del(keytree.NotifyVM)
}

func clearRoot(kv kvspace.KVSpace, root string) {
	children, _ := kv.List(root)
	for _, c := range children {
		delRecursive(kv, root+"/"+c)
	}
}

func delRecursive(kv kvspace.KVSpace, prefix string) {
	children, _ := kv.List(prefix)
	for _, c := range children {
		delRecursive(kv, prefix+"/"+c)
	}
	kv.Del(prefix)        // 删除自身
	kv.Del(prefix + "/.")  // 删除索引
}
