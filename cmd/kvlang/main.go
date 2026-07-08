// kvlang — KV 语言 VM 解释器 CLI。
//
//	kvlang                   启动 daemon
//	kvlang <file.kv>          执行文件
//	kvlang -c "code"          执行内联代码
//	echo "code" | kvlang       管道传入
package main

import "os"

func main() {
	cmdRun(os.Args[1:])
}
