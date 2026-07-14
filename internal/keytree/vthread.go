package keytree

import (
	"fmt"
	"strings"
)

const VthreadRoot  = "/vthread"
const VthreadSeq   = "/vthread/seq"   // vtid 序列计数器
const VthreadReady = "/vthread/ready" // 新 vthread 就绪通知队列

// VThread 返回 /vthread/<vtid>（vthread 根路径，用于 DelR 整体清理）
func VThread(vtid string) string { return "/vthread/" + vtid }

// VThreadTerm 返回 /vthread/<vtid>/term（绑定终端名，可选）
func VThreadTerm(vtid string) string { return "/vthread/" + vtid + "/term" }

// ── vthread 引擎保留字段（. 开头，类比 Linux 隐藏文件）────────────────────────
//
// 仅三个字段，精确对应 def main()->(.status:str) 的语义：
//
//	.pc                   当前绝对 PC（String）
//	.status               生命周期状态（String: init|running|wait）；
//	                      终态时 Del+Notify：值为 main() 的返回值（如 "ok"/"error"）
//	.<statusVal>/msg      终态附加描述，路径随 status 值动态生成：
//	                        status="error"   → .error/msg   存错误详情
//	                        status="timeout" → .timeout/msg 存超时说明
//	                        status="ok"      → 通常不写（无需附加信息）
//
// kvlang 标识符不能以 . 开头，用户代码无法写入这些路径。

// VThreadPC 返回 /vthread/<vtid>/.pc
func VThreadPC(vtid string) string { return "/vthread/" + vtid + "/.pc" }

// VThreadStatus 返回 /vthread/<vtid>/.status
//
//   - 运行期（String）：init | running | wait
//   - 终态（List/Notify）：main() 的返回值，由 WaitDone Watch 消费
func VThreadStatus(vtid string) string { return "/vthread/" + vtid + "/.status" }

// VThreadStatusMsg 返回 /vthread/<vtid>/.<statusVal>/msg
//
// 终态附加描述，路径随 status 值动态生成。
// 例：
//
//	VThreadStatusMsg(vtid, "error")   → /vthread/<vtid>/.error/msg
//	VThreadStatusMsg(vtid, "timeout") → /vthread/<vtid>/.timeout/msg
func VThreadStatusMsg(vtid, statusVal string) string {
	return "/vthread/" + vtid + "/." + statusVal + "/msg"
}

// VThreadAt 返回 /vthread/<vtid>/<key>，通用路径构造（仅供引擎内部调试使用）
func VThreadAt(vtid, key string) string { return "/vthread/" + vtid + "/" + key }

// VtidFromPC 从绝对 PC 提取 vtid。
//
//	"/vthread/42/[0,0]" → "42"
//	"/vthread/42/[3,0]/[0,0]" → "42"
func VtidFromPC(pc string) string {
	const pfx = "/vthread/"
	if !strings.HasPrefix(pc, pfx) {
		return ""
	}
	rest := pc[len(pfx):]
	slash := strings.Index(rest, "/")
	if slash < 0 {
		return rest
	}
	return rest[:slash]
}

// VThreadSlot 返回指令槽路径。
//
//	frame="" → /vthread/<vtid>/[i,j]
//	frame="[0,0]" → /vthread/<vtid>/[0,0]/[i,j]
func VThreadSlot(vtid, frame string, i, j int) string {
	if frame == "" {
		return fmt.Sprintf("/vthread/%s/[%d,%d]", vtid, i, j)
	}
	return fmt.Sprintf("/vthread/%s/%s/[%d,%d]", vtid, frame, i, j)
}

// VThreadFrame 返回子帧路径前缀（不含尾斜杠）。
//
//	frame="" → /vthread/<vtid>
//	frame="[0,0]" → /vthread/<vtid>/[0,0]
func VThreadFrame(vtid, frame string) string {
	if frame == "" {
		return "/vthread/" + vtid
	}
	return "/vthread/" + vtid + "/" + frame
}
