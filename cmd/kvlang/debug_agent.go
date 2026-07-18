package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"time"

	"kvlang/internal/keytree"
	"github.com/array2d/kvlang-go"
	"kvlang/internal/logx"
)

// debugPauseEvent 是 CPU 推送到 .debug.pause 的 JSON 结构。
type debugPauseEvent struct {
	PC    string `json:"pc"`
	Func  string `json:"func"`
	Frame string `json:"frame"`
	Op    string `json:"op"`
}

// runDebugAgent 启动本地交互式调试代理，运行于独立 goroutine。
//
// 使用方式：在 executeEntry 之前设置 .debug = "step"，
// 然后调用 go runDebugAgent(kv, vtid, done)；
// 执行完成后向 done 发送信号，agent 自动退出。
//
// 交互命令（从 stdin 读取，空行 = step）：
//
//	<Enter>  — 单步执行下一条指令
//	c        — 取消单步，全速继续执行
//	q        — 中止程序
func runDebugAgent(kv kvspace.KVSpace, vtid string, done <-chan struct{}) {
	pauseKey := keytree.VThreadDebugPause(vtid)
	resumeKey := keytree.VThreadDebugResume(vtid)
	step := 0
	reader := bufio.NewReader(os.Stdin)

	for {
		// 等待 CPU 暂停事件（1s 超时轮询，同时检查 done）
		val, err := kv.Watch(pauseKey, time.Second)
		if err != nil {
			select {
			case <-done:
				return
			default:
				continue
			}
		}

		step++
		var ev debugPauseEvent
		if jsonErr := json.Unmarshal([]byte(val.Str()), &ev); jsonErr != nil {
			ev.Op = val.Str()
		}

		// 打印暂停事件到 stderr
		fmt.Fprintf(os.Stderr, "\n[dbg #%d] op=%-10s func=%-12s\n", step, ev.Op, ev.Func)
		fmt.Fprintf(os.Stderr, "         pc=%s\n", ev.PC)
		fmt.Fprintf(os.Stderr, "  [Enter]=step  c=continue  q=abort  ? ")

		// 读取用户命令
		line, _ := reader.ReadString('\n')
		cmd := strings.TrimSpace(strings.ToLower(line))

		switch cmd {
		case "c", "continue":
			logx.Debug("[debugAgent] continue")
			// 清除 .debug 标志，CPU 会退出单步模式
			kv.Del(keytree.VThreadDebug(vtid))
			kv.Notify(resumeKey, kvspace.Str("continue"))
			fmt.Fprintln(os.Stderr, "  [continuing at full speed]")
			// agent 继续监听（CPU 可能在函数入口重新激活调试）
		case "q", "quit", "abort":
			logx.Debug("[debugAgent] abort")
			kv.Notify(resumeKey, kvspace.Str("abort"))
			fmt.Fprintln(os.Stderr, "  [aborted]")
			return
		default:
			// 空行或其他 → 单步
			kv.Notify(resumeKey, kvspace.Str("step"))
		}
	}
}
