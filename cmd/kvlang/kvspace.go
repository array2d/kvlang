package main

import (
	"flag"
	"fmt"
	"os"
	"strings"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

func cmdKVSpace(args []string) {
	// 全局 FlagSet：解析 --addr 及子命令
	fs := flag.NewFlagSet("kvspace", flag.ExitOnError)
	addr := fs.String("addr", "127.0.0.1:6379", "Redis 地址 (host:port)")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang kvspace [--addr host:port] <subcommand> [args]")
		fmt.Fprintln(os.Stderr, "subcommands: get mget set del list tree dump watch notify clear")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	sub := fs.Args() // 子命令及其参数
	if len(sub) == 0 {
		fs.Usage()
		os.Exit(1)
	}

	kv := kvspace.Conn(*addr)
	defer kv.DisConn()

	switch sub[0] {
	case "get":
		if len(sub) < 2 { usageExit("kvlang kvspace get <key>") }
		val, err := kv.Get(sub[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		fmt.Println(val)

	case "mget":
		if len(sub) < 2 { usageExit("kvlang kvspace mget <key1> <key2> ...") }
		vals, err := kv.Gets(sub[1:]...)
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for i, v := range vals {
			if v == "" { fmt.Printf("%s\t(nil)\n", sub[i+1]) } else { fmt.Printf("%s\t%s\n", sub[i+1], v) }
		}

	case "set":
		if len(sub) < 3 { usageExit("kvlang kvspace set <key> <value>") }
		kv.Set(sub[1], sub[2])

	case "del":
		if len(sub) < 2 { usageExit("kvlang kvspace del <key1> [key2 ...]") }
		kv.Del(sub[1:]...)

	case "list":
		if len(sub) < 2 { usageExit("kvlang kvspace list <prefix>") }
		children, err := kv.List(sub[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for _, c := range children { fmt.Println(c) }

	case "tree":
		if len(sub) < 2 { usageExit("kvlang kvspace tree <prefix>") }
		fmt.Println(sub[1])
		printTree(kv, sub[1], "")

	case "dump":
		if len(sub) < 2 { usageExit("kvlang kvspace dump <prefix>") }
		dumpPrefix(kv, sub[1])

	case "watch":
		kvWatch(kv, sub[1:])

	case "notify":
		if len(sub) < 3 { usageExit("kvlang kvspace notify <key> <value>") }
		if err := kv.Notify(sub[1], sub[2]); err != nil {
			fmt.Fprintln(os.Stderr, err); os.Exit(1)
		}

	case "clear":
		clearAll(kv)

	default:
		fmt.Fprintf(os.Stderr, "unknown kvspace subcommand: %s\n", sub[0])
		os.Exit(1)
	}
}

// kvWatch 使用独立 FlagSet 解析 --timeout。
func kvWatch(kv kvspace.KVSpace, args []string) {
	fs := flag.NewFlagSet("watch", flag.ExitOnError)
	timeout := fs.Duration("timeout", 0, "等待超时（如 5s、1m）；0 表示永久阻塞")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang kvspace watch [--timeout duration] <key>")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	if fs.NArg() == 0 {
		fs.Usage()
		os.Exit(1)
	}
	key := fs.Arg(0)
	result, err := kv.Watch(key, *timeout)
	if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
	fmt.Println(result)
}

func usageExit(msg string) {
	fmt.Fprintln(os.Stderr, "usage:", msg)
	os.Exit(1)
}

// clearAll 清空所有已知 KV 根路径。
func clearAll(kv kvspace.KVSpace) {
	for _, root := range []string{
		keytree.VthreadRoot,
		keytree.SrcRoot,
		keytree.FuncRoot,
		keytree.SysRoot,
		"/dev",
	} {
		children, _ := kv.List(root)
		for _, c := range children {
			kv.DelR(root + "/" + c)
		}
	}
}

func printTree(kv kvspace.KVSpace, prefix, indent string) {
	children, _ := kv.List(prefix)
	for i, c := range children {
		last := i == len(children)-1
		branch := "├── "
		if last { branch = "└── " }
		fmt.Printf("%s%s%s\n", indent, branch, c)
		next := indent + "│   "
		if last { next = indent + "    " }
		printTree(kv, prefix+"/"+c, next)
	}
}

func dumpPrefix(kv kvspace.KVSpace, prefix string) {
	if val, err := kv.Get(prefix); err == nil {
		short := strings.ReplaceAll(val, "\n", "↵")
		if len(short) > 80 { short = short[:80] + "…" }
		fmt.Printf("%-60s %s\n", prefix, short)
	}
	children, _ := kv.List(prefix)
	for _, c := range children { dumpPrefix(kv, prefix+"/"+c) }
}
