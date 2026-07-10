package main

import (
	"fmt"
	"os"
	"strings"
	"time"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

func cmdKVSpace(args []string) {
	// 解析全局 --addr 标志
	addr := "127.0.0.1:6379"
	var rest []string
	for i := 0; i < len(args); i++ {
		if args[i] == "--addr" {
			if i+1 >= len(args) || !strings.Contains(args[i+1], ":") {
				fmt.Fprintln(os.Stderr, "usage: --addr requires a host:port argument")
				os.Exit(1)
			}
			addr = args[i+1]
			i++
		} else {
			rest = append(rest, args[i])
		}
	}
	args = rest

	if len(args) < 1 {
		fmt.Fprintln(os.Stderr, "usage: kvlang kvspace [--addr host:port] <get|mget|set|del|list|tree|dump|watch|notify|clear> [args]")
		os.Exit(1)
	}

	kv := kvspace.Conn(addr)
	defer kv.DisConn()

	switch args[0] {
	case "get":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace get <key>"); os.Exit(1) }
		val, err := kv.Get(args[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		fmt.Println(val)

	case "mget":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace mget <key1> <key2> ..."); os.Exit(1) }
		vals, err := kv.MGet(args[1:]...)
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for i, v := range vals {
			if v == nil {
				fmt.Printf("%s\t(nil)\n", args[i+1])
			} else {
				fmt.Printf("%s\t%v\n", args[i+1], v)
			}
		}

	case "set":
		if len(args) < 3 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace set <key> <value>"); os.Exit(1) }
		kv.Set(args[1], args[2], 0)

	case "del":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace del <key1> [key2 ...]"); os.Exit(1) }
		kv.Del(args[1:]...)

	case "list":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace list <prefix>"); os.Exit(1) }
		children, err := kv.List(args[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for _, c := range children { fmt.Println(c) }

	case "tree":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace tree <prefix>"); os.Exit(1) }
		fmt.Println(args[1])
		printTree(kv, args[1], "")

	case "dump":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace dump <prefix>"); os.Exit(1) }
		dumpPrefix(kv, args[1])

	case "watch":
		if len(args) < 2 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace watch <key> [--timeout 5s]"); os.Exit(1) }
		key := args[1]
		timeout := time.Duration(0)
		for i := 2; i+1 < len(args); i++ {
			if args[i] == "--timeout" {
				d, err := time.ParseDuration(args[i+1])
				if err != nil {
					fmt.Fprintf(os.Stderr, "invalid --timeout %q: %v\n", args[i+1], err)
					os.Exit(1)
				}
				timeout = d
				i++
			}
		}
		results, err := kv.Watch(timeout, key)
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for _, r := range results { fmt.Println(r) }

	case "notify":
		if len(args) < 3 { fmt.Fprintln(os.Stderr, "usage: kvlang kvspace notify <key> <value>"); os.Exit(1) }
		if err := kv.Notify(args[1], args[2]); err != nil {
			fmt.Fprintln(os.Stderr, err); os.Exit(1)
		}

	case "clear":
		clearAll(kv)

	default:
		fmt.Fprintf(os.Stderr, "unknown kvspace subcommand: %s\n", args[0])
		os.Exit(1)
	}
}

// clearAll 清空所有已知 KV 根路径。
func clearAll(kv kvspace.KVSpace) {
	for _, root := range []string{
		keytree.VthreadRoot,
		keytree.SrcRoot,
		keytree.FuncRoot,
		keytree.SysRoot,
		keytree.OpRoot,
		keytree.NotifyRoot,
		keytree.DoneRoot,
	} {
		clearRoot(kv, root)
	}
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
	kv.Del(prefix)
	kv.Del(prefix + "/.")
}

// printTree 递归打印 prefix 下的 key 树形结构。
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

// dumpPrefix 递归打印 prefix 下所有 key 及其值。
func dumpPrefix(kv kvspace.KVSpace, prefix string) {
	if val, err := kv.Get(prefix); err == nil {
		short := strings.ReplaceAll(val, "\n", "↵")
		if len(short) > 80 { short = short[:80] + "…" }
		fmt.Printf("%-60s %s\n", prefix, short)
	}
	children, _ := kv.List(prefix)
	for _, c := range children {
		dumpPrefix(kv, prefix+"/"+c)
	}
}
