package main

import (
	"fmt"
	"os"
)

const helpText = `kvlang — KV language VM interpreter

usage:
  kvlang layoutrwir <file|dir>…                   装载到 /lib/（多文件拼接为单源，不执行）
  kvlang layoutrwirandrun <file|dir>… [--debug]   先 layoutrwir 再 run
  kvlang run [{lib}.{func}]                 执行 /lib/{lib}.{func}（默认 .init，无参=匿名 lib）
  kvlang -c "code" [--debug]               内联执行
  echo "code" | kvlang                      管道执行（stdin 非终端）

  kvlang vet [--dump] [--lower] [-c code | <file.kv>]  语法检查
  kvlang format [-w] [-c code | <file.kv>]  格式化（别名 fmt；默认打印，-w 原地写回）
  kvlang help                                显示此帮助

KV 空间操作已迁至独立 CLI（kvlang-go 仓 cmd/kvspace）:
  kvspace [--kvspace dsn] <get|mget|set|del|list|tree|dump|watch|notify|clear>

选项:
  --kvspace <dsn>                kvspace 地址（默认 redis://127.0.0.1:6379；KVLANG_KVSPACE 可覆盖）
  --debug                        单步调试模式（设 .debugger="step"，agent 通过 kvspace 协议控制）

示例:
  kvlang layoutrwirandrun file.kv             装载并执行
  kvlang layoutrwir lib.kv                    仅装载不执行
  kvlang run math.sum                   执行 /lib/math.sum
  kvlang -c 'x = 40 + 2; print(x)'      内联执行（= 等价于 <-）
  kvlang vet -c 'a = { k=1 }'           语法检查内联代码
  kvspace dump /lib                      查看已加载函数
`

func showHelp() {
	fmt.Fprint(os.Stderr, helpText)
	os.Exit(0)
}
