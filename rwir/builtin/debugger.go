package builtin

import (
	"encoding/json"
	"time"

	"kvlang/keytree"
	"kvlang/rwir"
	"kvlang/vthread"

	"github.com/array2d/kvspace-go"
)

func init() {
	Register("debugger", "", debuggerOp{})
}

// debuggerOp: debugger() —— 内联暂停点（tothink-031，对齐 V8/TypeScript `debugger;` 语句）。
// 非调试模式下（.debugger 为空）为 no-op；调试模式下暂停当前 vthread 等待 agent 命令。
// 暂停/恢复逻辑内联于此（不 import kvcpu 以避免循环依赖：kvcpu → builtin）。
type debuggerOp struct{}
func (debuggerOp) Call(f *rwir.Frame) error {
	debugKey := keytree.VThreadDebugger(f.Vtid)
	v := kvspace.GetOne(f.KV, debugKey)
	if kvspace.IsNone(v) {
		// 非调试模式：no-op
		vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
		return nil
	}
	// 写暂停位置（agent 轮询读取）
	pauseKey := keytree.VThreadDebuggerPause(f.Vtid)
	info, _ := json.Marshal(map[string]string{
		"pc": f.PC, "vtid": f.Vtid, "opcode": f.Inst.Opcode,
		"func": "", "frame": keytree.FrameRoot(f.PC),
	})
	f.KV.Set([]kvspace.KVPair{{Key: pauseKey, Val: kvspace.NewCharByte(info...)}})

	// 轮询等待 agent 命令
	resumeKey := keytree.VThreadDebuggerResume(f.Vtid)
	for {
		cmd := kvspace.GetOne(f.KV, resumeKey).ValueString()
		if cmd == "" {
			time.Sleep(time.Millisecond)
			continue
		}
		f.KV.Del(resumeKey) // 消费命令
		switch cmd {
		case "abort":
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, "debugger: aborted by agent")
			return nil
		case "continue":
			f.KV.Del(debugKey)
			vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
			return nil
		default:
			// "step" 或其他 → 单步到下一条指令
			vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
			return nil
		}
	}
}
