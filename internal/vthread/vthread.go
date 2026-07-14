// Package vthread 管理 vthread 在 kvspace 中的状态存储。
//
// 设计原则（对齐 kvcpu/最高标准设计.md §2.1）：
//   - key = keytree 具名路径，value = 标量字符串，严禁 JSON 对象
//   - 引擎保留键以 . 开头（.pc / .status），类比 Linux 隐藏文件
//   - PC 始终为绝对路径，格式：/vthread/<vtid>/[i,0][/[j,0]]...
//
// # 系统字段（仅三个）
//
//	.pc                   当前绝对 PC（String）
//	.status               生命周期状态（String: init|running|wait）；
//	                      终态时 Del+Notify：值为 main() 返回值（如 "ok"/"error"）
//	.<statusVal>/msg      终态附加描述，路径随 status 值动态生成（正常运行时不存在）：
//	                        status="error"   → .error/msg   存错误详情
//	                        status="timeout" → .timeout/msg 存超时说明
//	                        status="ok"      → 通常不写（无需附加信息）
//
// # PC 更新契约
//
// 所有对 .pc 的变更**必须**通过本包的 Set / SetDone / SetError 函数完成。
// 任何绕过这三个函数直接调用 kv.Set(keytree.VThreadPC(...), ...) 的代码
// 都会破坏 Execute 循环在每轮末尾读取 .pc 的假设，导致 PC 滞后或跳变。
//
// 各函数的 PC 语义：
//   - Set(vtid, pc, status)    — 写入新 PC + 新 status（用于 running/wait）
//   - SetDone(vtid, retVal)    — 不改 PC；Del(.status) + Notify(.status, retVal)
//   - SetError(vtid, pc, msg)  — 写入当前 PC + 写 .<status>/msg + Del + Notify("error")
package vthread

import (
	"context"
	"fmt"
	"time"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

// ── 读 ────────────────────────────────────────────────────────────────────

// Get 读取 vthread 的当前 PC 和 status。
// 若读取失败或 vthread 不存在，返回 ("", "")。
func Get(ctx context.Context, kv kvspace.KVSpace, vtid string) (pc, status string) {
	vals, err := kv.Gets(keytree.VThreadPC(vtid), keytree.VThreadStatus(vtid))
	if err != nil || len(vals) < 2 {
		return "", ""
	}
	return vals[0], vals[1]
}

// ── 写（瞬态） ─────────────────────────────────────────────────────────────

// Set 更新 vthread 的 PC 和 status（瞬态：init / running / wait）。
// 终态请用 SetDone / SetError，它们使用 Del+Notify 而非 Set。
func Set(ctx context.Context, kv kvspace.KVSpace, vtid, pc, status string) {
	kv.Set(keytree.VThreadPC(vtid), pc)
	kv.Set(keytree.VThreadStatus(vtid), status)
}

// ── 写（终态）─────────────────────────────────────────────────────────────

// SetDone 标记 vthread 正常完成。
//
// retVal 为 main() 的返回值（如 "ok"、"not_found"），成为 .status 的终态通知值。
// WaitDone 的调用方将收到该值。
//
// 流程：Del(.status) → Notify(.status, retVal)
// Del 将 String 类型清除，Notify 写入 List 供 Watch/BLPOP 消费。
func SetDone(ctx context.Context, kv kvspace.KVSpace, vtid, retVal string) {
	if retVal == "" {
		retVal = "ok" // 无显式返回值时默认为 "ok"
	}
	kv.Del(keytree.VThreadStatus(vtid))
	kv.Notify(keytree.VThreadStatus(vtid), retVal)
}

// SetError 标记 vthread 错误终止。
//
// 流程：Set(.pc, pc) → Set(.<errcode>/msg, errMsg) → Del(.status) → Notify(.status, "error")
//
// 错误详情存于 .error/msg（即 VThreadStatusMsg(vtid, "error")），
// WaitDone 收到 "error" 信号后可从该路径读取详情。
func SetError(ctx context.Context, kv kvspace.KVSpace, vtid, pc, errMsg string) {
	kv.Set(keytree.VThreadPC(vtid), pc)
	kv.Set(keytree.VThreadStatusMsg(vtid, "error"), errMsg)
	kv.Del(keytree.VThreadStatus(vtid))
	kv.Notify(keytree.VThreadStatus(vtid), "error")
}

// ── 生命周期 ──────────────────────────────────────────────────────────────

// CreateVThread 在 kvspace 中创建新虚线程，返回 vtid。
//
// 写入的初始状态（平铺 key，无 JSON）：
//
//	.pc     = /vthread/<vtid>/[0,0]  ← 绝对路径
//	.status = init
//	[0,0]   = funcName              ← 入口函数 opcode
//	[0,-j]  = reads[j-1]            ← 入参读槽
//	[0,+j]  = writes[j-1]           ← 出参写槽
func CreateVThread(ctx context.Context, kv kvspace.KVSpace, funcName string, reads, writes []string) (string, error) {
	vtid := fmt.Sprintf("%d", time.Now().UnixNano())
	absPC := keytree.VThreadSlot(vtid, "", 0, 0) // /vthread/<vtid>/[0,0]

	if err := kv.Set(keytree.VThreadPC(vtid), absPC); err != nil {
		return "", fmt.Errorf("vthread.Create: set .pc: %w", err)
	}
	kv.Set(keytree.VThreadStatus(vtid), "init")
	kv.Set(keytree.VThreadSlot(vtid, "", 0, 0), funcName)
	for i, r := range reads {
		kv.Set(keytree.VThreadSlot(vtid, "", 0, -(i+1)), r)
	}
	for i, w := range writes {
		kv.Set(keytree.VThreadSlot(vtid, "", 0, i+1), w)
	}
	return vtid, nil
}

// ── 等待 ──────────────────────────────────────────────────────────────────

// WaitDone 阻塞等待 vthread 终态。
//
// 返回值：
//   - (retVal, nil)  — 终态值（如 "ok"、"not_found"）；对应 SetDone(vtid, retVal)
//   - ("", error)    — 错误终止；error 包含 .error/msg 的内容
//   - ("", timeout)  — 超时
//
// 实现：Watch .status（List BLPOP）；收到 "error" 时额外读 .error/msg。
func WaitDone(ctx context.Context, kv kvspace.KVSpace, vtid string, timeout time.Duration) (string, error) {
	signal, err := kv.Watch(keytree.VThreadStatus(vtid), timeout)
	if err != nil {
		return "", fmt.Errorf("WaitDone %s: %w", vtid, err)
	}
	if signal == "error" {
		msg, _ := kv.Get(keytree.VThreadStatusMsg(vtid, "error"))
		return "", fmt.Errorf("vthread %s: %s", vtid, msg)
	}
	return signal, nil
}
