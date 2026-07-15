// kvlang — KV 语言 VM 解释器 CLI。
//
//	kvlang                        启动 daemon（serve 模式）
//	kvlang serve                  启动 daemon（显式）
//	kvlang <file.kv>              加载并执行文件
//	kvlang -c "code"              加载并执行内联代码
//	echo "code" | kvlang          加载并执行管道代码
//	kvlang load <file.kv|dir>     只加载到 kvspace，不执行
//	kvlang vet <file.kv>          语法检查
//	kvlang format <file.kv>       格式化
//	kvlang kvspace <cmd>          KV 空间操作
//	kvlang help                   帮助
package main

import (
	"os"

	// 注册 KVSpace 实现；KVLANG_KV 环境变量选择后端（默认 "redis"）。
	_ "kvlang/internal/kvspace/redis"
)

func main() {
	args := os.Args[1:]
	if len(args) > 0 {
		switch args[0] {
		case "serve":
			runServe(args[1:])
			return
		case "load":
			cmdLoad(args[1:])
			return
		case "vet":
			cmdVet(args[1:])
			return
		case "kvspace":
			cmdKVSpace(args[1:])
			return
		case "format", "fmt":
			cmdFormat(args[1:])
			return
		case "help", "-h", "--help":
			showHelp()
			return
		}
	}
	// 无子命令: -c "code", <file.kv>, stdin 管道 → run；无参数且无管道 → serve
	cmdRun(args)
}
