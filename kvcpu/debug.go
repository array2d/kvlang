// debug.go: kvcpu 内联调试辅助函数。
//
// 所有 cpu.Execute 循环都自动包含调试检查点，agent 只需通过已有
// kvspace 命令读写 keytree.VThreadDebugger* 键即可控制调试行为。
// 无需特殊启动方式，对任何正在运行的 kv 程序均有效。
package kvcpu

import (
	"context"
	"encoding/json"
	"strings"
	"time"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
)

// debugFuncName 从帧根目录的 extindex 目标路径提取函数名。
// extindex → /lib/<name>/ → 返回 <name>。
func debugFuncName(kv kvspace.KVSpace, frameRoot string) string {
	parent, dirName := kvspace.SepPath(frameRoot)
	if parent != "/" { parent += kvspace.DirIndexSuf }
	v := kv.Get(parent, []string{dirName + kvspace.DirIndexSuf}, true)[0]
	extTarget := kvspace.DecodeExtIndex(kvspace.BodyBytes(v)).ExtPath()
	if extTarget == "" { return "?" }
	name := strings.TrimPrefix(extTarget, keytree.LibRoot+"/")
	return strings.TrimSuffix(name, "/")
}

// debugNotifyPause 向 /vthread/<vtid>/.debugger.pause 写暂停事件（JSON）。
// CPU 命中断点后调用，agent 通过 kvspace 轮询读取。
func debugNotifyPause(_ context.Context, kv kvspace.KVSpace, vtid, pc string, inst *rwir.Rwir) {
	frameRoot := keytree.FrameRoot(pc)
	event, _ := json.Marshal(map[string]any{
		"pc":    pc,
		"func":  debugFuncName(kv, frameRoot),
		"frame": frameRoot,
		"op":    inst.Opcode,
	})
	kv.Set([]kvspace.KVPair{{Key: keytree.VThreadDebuggerPause(vtid), Val: kvspace.NewCharByte(event...)}})
}

// debugWaitResume 轮询 /vthread/<vtid>/.debugger.resume，
// 返回 agent 发送的命令字符串（"step" / "continue" / "abort"）。
func debugWaitResume(kv kvspace.KVSpace, vtid string) string {
	resumeKey := keytree.VThreadDebuggerResume(vtid)
	for {
		cmd := kvspace.GetOne(kv, resumeKey).ValueString()
		if cmd != "" {
			kv.Del(resumeKey) // 消费命令
			return cmd
		}
		time.Sleep(time.Millisecond)
	}
}
