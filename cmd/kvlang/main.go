// kvlang — KV 语言 VM 解释器 CLI。
//
// 子命令:
//
//	kvlang serve [addr]        启动 VM 服务端
//	kvlang run <path|vtid> [addr]  加载并执行 .kv 文件或 vthread
package main

import (
	"fmt"
	"os"
)

func main() {
	if len(os.Args) < 2 {
		printUsage()
		os.Exit(1)
	}
	switch os.Args[1] {
	case "serve":
		cmdServe(os.Args[2:])
	case "run":
		cmdRun(os.Args[2:])
	default:
		fmt.Fprintf(os.Stderr, "unknown command: %s\n", os.Args[1])
		os.Exit(1)
	}
}

func printUsage() {
	fmt.Fprintln(os.Stderr, "kvlang — KV language VM interpreter")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "usage: kvlang <command> [args]")
	fmt.Fprintln(os.Stderr, "  serve [addr]              start VM server")
	fmt.Fprintln(os.Stderr, "  run <file.kv|vtid> [addr]  load & execute")
}
