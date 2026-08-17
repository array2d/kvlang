package main

import "os"

// isTerminal 判断 stdin 是否为交互式终端（字符设备）。
// 管道 / socket / 文件重定向时返回 false。
func isTerminal() bool {
	stat, _ := os.Stdin.Stat()
	return (stat.Mode() & os.ModeCharDevice) != 0
}
