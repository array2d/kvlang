// kvlang — KV 语言 VM 解释器 CLI。
//
//	kvlang run              启动 VM 服务端 (daemon)
//	kvlang run <file.kv>     加载并执行
package main

import (
	"fmt"
	"os"
)

func main() {
	if len(os.Args) < 2 || os.Args[1] != "run" {
		fmt.Fprintln(os.Stderr, "kvlang — KV language VM interpreter")
		fmt.Fprintln(os.Stderr)
		fmt.Fprintln(os.Stderr, "usage: kvlang run [<file.kv>]")
		fmt.Fprintln(os.Stderr, "  (no args)   start daemon")
		fmt.Fprintln(os.Stderr, "  <file.kv>   load & execute")
		os.Exit(1)
	}
	cmdRun(os.Args[2:])
}
