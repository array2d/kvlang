package keytree

import "fmt"

const VthreadRoot  = "/vthread"
const VthreadSeq   = "/vthread/seq"   // vtid 序列计数器
const VthreadReady = "/vthread/ready" // 新 vthread 就绪通知队列

// VThread 返回 /vthread/<vtid>（vthread 根路径，用于 DelR 整体清理）
func VThread(vtid string) string { return "/vthread/" + vtid }

// VThreadTerm 返回 /vthread/<vtid>/term（绑定终端名，可选）
func VThreadTerm(vtid string) string { return "/vthread/" + vtid + "/term" }

// ── vthread 引擎状态路径（. 开头为保留键，类比 Linux 隐藏文件）──────────────────
//
// kvlang 标识符不能以 . 开头，用户代码无法写入这些路径：
//   - 第一层隔离：解析阶段拒绝 .xxx 标识符
//   - 第二层隔离：用户帧变量始终在 [i,j] 帧前缀之下，结构上也不会冲突

// VThreadPC 返回 /vthread/<vtid>/.pc
func VThreadPC(vtid string) string { return "/vthread/" + vtid + "/.pc" }

// VThreadStatus 返回 /vthread/<vtid>/.status（init|running|wait|done|error）
func VThreadStatus(vtid string) string { return "/vthread/" + vtid + "/.status" }

// VThreadMode 返回 /vthread/<vtid>/.mode（single|batch）
func VThreadMode(vtid string) string { return "/vthread/" + vtid + "/.mode" }

// VThreadErr 返回 /vthread/<vtid>/.err（错误码；存在即表示 error 状态）
func VThreadErr(vtid string) string { return "/vthread/" + vtid + "/.err" }

// VThreadErrMsg 返回 /vthread/<vtid>/.err/msg
func VThreadErrMsg(vtid string) string { return "/vthread/" + vtid + "/.err/msg" }

// VThreadDone 返回 /vthread/<vtid>/.done（完成信号，值为 "ok" 或 "error"）
func VThreadDone(vtid string) string { return "/vthread/" + vtid + "/.done" }

// VThreadDoneResult 返回 /vthread/<vtid>/.done/result（done=ok 时的返回值）
func VThreadDoneResult(vtid string) string { return "/vthread/" + vtid + "/.done/result" }

// VThreadAt 返回 /vthread/<vtid>/<key>，通用路径构造（仅供引擎内部调试使用）
func VThreadAt(vtid, key string) string { return "/vthread/" + vtid + "/" + key }

// VThreadSlot 返回指令槽路径。
//   frame="" → /vthread/<vtid>/[i,j]
//   frame="[0,0]" → /vthread/<vtid>/[0,0]/[i,j]
func VThreadSlot(vtid, frame string, i, j int) string {
	if frame == "" {
		return fmt.Sprintf("/vthread/%s/[%d,%d]", vtid, i, j)
	}
	return fmt.Sprintf("/vthread/%s/%s/[%d,%d]", vtid, frame, i, j)
}

// VThreadFrame 返回子帧路径前缀（不含尾斜杠）。
//   frame="" → /vthread/<vtid>
//   frame="[0,0]" → /vthread/<vtid>/[0,0]
func VThreadFrame(vtid, frame string) string {
	if frame == "" {
		return "/vthread/" + vtid
	}
	return "/vthread/" + vtid + "/" + frame
}
