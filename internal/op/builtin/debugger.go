package builtin

import (
	"encoding/json"

	"kvlang/internal/keytree"
	"kvlang/internal/op"
	"kvlang/internal/vthread"

	"github.com/array2d/kvlang-go"
)

// debuggerOp: debugger() —— 内联暂停点（tothink-031，对齐 V8/TypeScript `debugger;` 语句）。
// 非调试模式下（.debugger 为空）为 no-op；调试模式下暂停当前 vthread 等待 agent 命令。
// 暂停/恢复逻辑内联于此（不 import kvcpu 以避免循环依赖：kvcpu → builtin）。
type debuggerOp struct{}
func (debuggerOp) Call(f *op.Frame) error {
	debugKey := keytree.VThreadDebugger(f.Vtid)
	v, _ := f.KV.Get(debugKey)
	if v.IsNil() {
		// 非调试模式：no-op
		vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
		return nil
	}
	// 通知 agent 暂停位置
	pauseKey := keytree.VThreadDebuggerPause(f.Vtid)
	info, _ := json.Marshal(map[string]string{
		"pc": f.PC, "vtid": f.Vtid, "opcode": f.Inst.Opcode,
		"func": "", "frame": keytree.FrameRoot(f.PC),
	})
	f.KV.Notify(pauseKey, kvspace.Bytes(info))

	// 阻塞等待 agent 命令
	resumeKey := keytree.VThreadDebuggerResume(f.Vtid)
	for {
		cmdVal, err := f.KV.Watch(resumeKey, 0)
		if err != nil {
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, "debugger: Watch failed: "+err.Error())
			return nil
		}
		switch cmdVal.Str() {
		case "abort":
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, "debugger: aborted by agent")
			return nil
		case "continue":
			f.KV.Del(debugKey)
			vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
			return nil
		default:
			// "step" 或其他 → 单步到下一条指令
			vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
			return nil
		}
	}
}
