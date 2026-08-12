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
	"strconv"
	"time"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
)

// ── 读 ────────────────────────────────────────────────────────────────────────

// Get 读取 vthread 的当前 PC 和 status。
func Get(ctx context.Context, kv kvspace.KVSpace, vtid string) (pc, status string) {
	pcV := kvspace.GetOne(kv, keytree.VThreadPC(vtid))
	stV := kvspace.GetOne(kv, keytree.VThreadStatus(vtid))
	return pcV.ValueString(), stV.ValueString()
}

// ── 写（瞬态） ────────────────────────────────────────────────────────────────

// Set 更新 vthread 的 PC 和 status（瞬态：init / running / wait），1 RTT。
func Set(ctx context.Context, kv kvspace.KVSpace, vtid, pc, status string) {
	kv.Set([]kvspace.KVPair{
		{keytree.VThreadPC(vtid), kvspace.NewChar(pc), -1},
		{keytree.VThreadStatus(vtid), kvspace.NewChar(status), -1},
	})
}

// ── 写（终态）─────────────────────────────────────────────────────────────────

// SetDone 标记 vthread 正常完成。
func SetDone(ctx context.Context, kv kvspace.KVSpace, vtid, retVal string) {
	if retVal == "" { retVal = "ok" }
	kv.Del(keytree.VThreadStatus(vtid))
	kv.Notify(keytree.VThreadStatus(vtid), kvspace.NewChar(retVal))
}

// SetError 标记 vthread 错误终止。MkIndexRecursive 确保 .error/ 目录存在（kvspace Set 要求父目录已存在）。
func SetError(ctx context.Context, kv kvspace.KVSpace, vtid, pc, errMsg string) {
	msgPath := keytree.VThreadStatusMsg(vtid, "error")
	prefix, _ := kvspace.SepPath(msgPath)
	kvspace.MkIndexRecursive(kv, prefix+kvspace.DirIndexSuf)
	kv.Set([]kvspace.KVPair{
		{keytree.VThreadPC(vtid), kvspace.NewChar(pc), -1},
		{msgPath, kvspace.NewChar(errMsg), -1},
	})
	kv.Del(keytree.VThreadStatus(vtid))
	kv.Notify(keytree.VThreadStatus(vtid), kvspace.NewChar("error"))
}

// ── 生命周期 ──────────────────────────────────────────────────────────────────

// AllocVtid 原子自增 /vthread/seq 并返回新 vtid。
func AllocVtid(kv kvspace.KVSpace) string {
	valV := kvspace.GetOne(kv, keytree.VthreadSeq)
	n, _ := strconv.ParseInt(valV.ValueString(), 10, 64)
	n++
	kv.Set([]kvspace.KVPair{{keytree.VthreadSeq, kvspace.NewChar(strconv.FormatInt(n, 10)), -1}})
	return fmt.Sprintf("%d", n)
}

// CreateVThread 在 kvspace 中创建新虚线程，返回 vtid。
func CreateVThread(kv kvspace.KVSpace, funcName string, reads, writes []string) (string, error) {
	vtid := fmt.Sprintf("%d", time.Now().UnixNano())
	absPC := keytree.VThreadSlot(vtid, "", 0, 0)

	pairs := []kvspace.KVPair{
		{Key: keytree.VThreadPC(vtid), Val: kvspace.NewChar(absPC), Arridx: -1},
		{Key: keytree.VThreadStatus(vtid), Val: kvspace.NewChar("init"), Arridx: -1},
		{Key: keytree.VThreadCtime(vtid), Val: kvspace.NewTime(time.Now().UnixNano()), Arridx: -1},
		{Key: keytree.VThreadSlot(vtid, "", 0, 0), Val: kvspace.NewChar(funcName), Arridx: -1},
	}
	for i, r := range reads {
		pairs = append(pairs, kvspace.KVPair{Key: keytree.VThreadSlot(vtid, "", 0, -(i + 1)), Val: kvspace.NewChar(r), Arridx: -1})
	}
	for i, w := range writes {
		pairs = append(pairs, kvspace.KVPair{Key: keytree.VThreadSlot(vtid, "", 0, i + 1), Val: kvspace.NewChar(w), Arridx: -1})
	}
	if err := kv.Set(pairs); err != nil {
		return "", fmt.Errorf("vthread.Create: %w", err)
	}
	return vtid, nil
}

// ── 等待 ──────────────────────────────────────────────────────────────────────

// WaitDone 阻塞等待 vthread 终态。
func WaitDone(ctx context.Context, kv kvspace.KVSpace, vtid string, timeout time.Duration) (string, error) {
	val := kv.Watch(keytree.VThreadStatus(vtid), timeout)
	signal := val.ValueString()
	if signal == "" {
		return "", fmt.Errorf("WaitDone %s: timeout", vtid)
	}
	if signal == "error" {
		msgVal := kvspace.GetOne(kv, keytree.VThreadStatusMsg(vtid, "error"))
		return "", fmt.Errorf("vthread %s: %s", vtid, msgVal.ValueString())
	}
	return signal, nil
}
