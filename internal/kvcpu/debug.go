// debug.go: kvcpu 内联调试辅助函数。
//
// 所有 cpu.Execute 循环都自动包含调试检查点，agent 只需通过已有
// kvspace 命令读写 keytree.VThreadDebugger* 键即可控制调试行为。
// 无需特殊启动方式，对任何正在运行的 kv 程序均有效。
package kvcpu

import (
	"context"
	"encoding/json"
	"time"

	"kvlang/internal/keytree"
	"github.com/array2d/kvlang-go"
	"kvlang/internal/op"
)

// isEntryPC 判断 pc 是否为帧入口指令（末尾坐标=[0,0]）。
func isEntryPC(pc string) bool { return keytree.IsEntryPC(pc) }

// debugFuncName 从帧根读取 .rootfunc 字段获取函数名。
func debugFuncName(kv kvspace.KVSpace, frameRoot string) string {
	v, err := kv.Get(keytree.RootFunc(frameRoot))
	if err != nil {
		return "?"
	}
	return v.Str()
}

// debugNotifyPause 向 /vthread/<vtid>/.debugger.pause 投递暂停事件（JSON）。
// CPU 命中断点后调用，agent 通过 kvspace watch 接收。
func debugNotifyPause(_ context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) {
	frameRoot := keytree.FrameRoot(pc)
	event, _ := json.Marshal(map[string]any{
		"pc":    pc,
		"func":  debugFuncName(kv, frameRoot),
		"frame": frameRoot,
		"op":    inst.Opcode,
	})
	kv.Notify(keytree.VThreadDebuggerPause(vtid), kvspace.Str(string(event)))
}

// debugWaitResume 阻塞等待 /vthread/<vtid>/.debugger.resume 上的 Notify，
// 返回 agent 发送的命令字符串（"step" / "continue" / "abort"）。
// 使用超时重试，与 vthread.WaitDone 保持一致的模式。
func debugWaitResume(kv kvspace.KVSpace, vtid string) string {
	for {
		val, err := kv.Watch(keytree.VThreadDebuggerResume(vtid), 30*time.Second)
		if err == nil {
			return val.Str()
		}
		// 超时 → 继续等待
	}
}
