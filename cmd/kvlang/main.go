// kvlang — KV 语言 VM 解释器 CLI。
//
//	kvlang                   启动 daemon
//	kvlang <file.kv>          执行文件
//	kvlang -c "code"          执行内联代码
//	kvlang vet <file.kv>      语法检查
//	echo "code" | kvlang       管道传入
package main

import "os"

func main() {
	args := os.Args[1:]
	if len(args) == 0 {
		cmdRun(args)
		return
	}
	switch args[0] {
	case "vet":
		cmdVet(args[1:])
	case "format", "fmt":
		cmdFormat(args[1:])
	default:
		cmdRun(args)
	}
}
