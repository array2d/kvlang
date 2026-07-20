// kvlang — KV 语言 VM 解释器 CLI。
//
//	kvlang <file.kv|dir>          加载并执行（目录递归收集 *.kv）
//	kvlang -c "code"              加载并执行内联代码
//	echo "code" | kvlang          执行管道代码（stdin 非终端）
//	kvlang                        无参数且 stdin 为终端 → 执行 .init
//	kvlang layoutrwir <file.kv|dir>     只加载到 kvspace，不执行
//	kvlang vet <file.kv>          语法检查
//	kvlang format <file.kv>       格式化（别名 fmt）
//	kvlang help                   帮助
package main

import (
	"os"

	// 注册 KVSpace 实现；--kvspace DSN 的 scheme 选择后端（默认 redis://）。
	_ "github.com/array2d/kvspace-go/redis"
)

func main() {
	args := os.Args[1:]
	if len(args) > 0 {
		switch args[0] {
		case "layoutrwir":
			cmdLayoutRWIR(args[1:])
			return
		case "layoutrwirandrun":
			cmdLayoutRWIRAndRun(args[1:])
			return
		case "run":
			cmdRun(args[1:])
			return
		case "vet":
			cmdVet(args[1:])
			return
		case "format", "fmt":
			cmdFormat(args[1:])
			return
		case "help", "-h", "--help":
			showHelp()
			return
		}
	}
	// 无子命令: -c "code", <file.kv>, stdin 管道 → run；无参数且无管道 → 执行 .init
	cmdRun(args)
}
