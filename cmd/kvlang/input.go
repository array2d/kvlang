package main

import (
	"io"
	"os"
	"strings"
)

// inputMode 输入来源。
type inputMode int

const (
	modeServe  inputMode = iota // 无参数, 无管道 → daemon
	modeFile                    // <file.kv> 文件
	modeInline                  // -c "code" 内联代码
	modePipe                    // stdin 管道
)

// parseInput 解析命令行参数，返回输入模式和对应的名称/reader/文件路径。
// cmd: "run" 或 "vet" (影响 modeServe 的默认行为)。
func parseInput(args []string, cmd string) (mode inputMode, name string, rc io.Reader) {
	switch {
	case len(args) >= 2 && args[0] == "-c":
		return modeInline, "inline", strings.NewReader(args[1])
	case len(args) == 0 && !isTerminal():
		return modePipe, "stdin", os.Stdin
	case len(args) == 0:
		return modeServe, "", nil
	default:
		return modeFile, args[0], nil
	}
}

func isTerminal() bool {
	stat, _ := os.Stdin.Stat()
	return (stat.Mode() & os.ModeCharDevice) != 0
}
