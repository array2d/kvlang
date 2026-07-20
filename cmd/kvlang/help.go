package main

import (
	"fmt"
	"os"
)

func showHelp() {
	fmt.Fprintln(os.Stderr, "kvlang — KV language VM interpreter")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "usage:")
	fmt.Fprintln(os.Stderr, "  kvlang loadandrun <file|dir>… [--debug]    先 load 再 run")
	fmt.Fprintln(os.Stderr, "  kvlang -c \"code\" [--debug]               内联执行")
	fmt.Fprintln(os.Stderr, "  echo \"code\" | kvlang                   管道执行（stdin 非终端）")
	fmt.Fprintln(os.Stderr, "  kvlang run                               无参数 → 执行 /lib/.init")
	fmt.Fprintln(os.Stderr, "  kvlang serve                             启动 VM daemon")
	fmt.Fprintln(os.Stderr, )
	fmt.Fprintln(os.Stderr, "  kvlang vet [--dump] [--lower] [-c code | <file.kv>]   语法检查")
	fmt.Fprintln(os.Stderr, "  kvlang format [-w] [-c code | <file.kv>] 格式化（别名 fmt；默认打印，-w 原地写回）")
	fmt.Fprintln(os.Stderr, "  kvlang help                              显示此帮助")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "KV 空间操作已迁至独立 CLI（kvlang-go 仓 cmd/kvspace）:")
	fmt.Fprintln(os.Stderr, "  kvspace [--kvspace dsn] <get|mget|set|del|list|tree|dump|watch|notify|clear>")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "选项:")
	fmt.Fprintln(os.Stderr, "  --kvspace <dsn>                kvspace 地址（默认 redis://127.0.0.1:6379，KVLANG_KVSPACE 可覆盖默认；")
	fmt.Fprintln(os.Stderr, "                                 裸 host:port 视为 redis；run/serve/load 支持）")
	fmt.Fprintln(os.Stderr, "  --debug                        单步调试模式（run 模式，交互式逐指令暂停）")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "示例:")
	fmt.Fprintln(os.Stderr, "  kvlang file.kv                    执行文件")
	fmt.Fprintln(os.Stderr, "  kvlang -c 'x = 40 + 2; print(x)'  内联执行（= 等价于 <-）")
	fmt.Fprintln(os.Stderr, "  echo 'print(\"hi\")' | kvlang       管道执行")
	fmt.Fprintln(os.Stderr, "  kvlang vet -c 'a = { k=1 }'       语法检查内联代码")
	fmt.Fprintln(os.Stderr, "  kvspace dump /src                 查看已加载函数源码（独立 CLI）")
	os.Exit(0)
}
