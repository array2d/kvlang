package keytree

import "fmt"

const VthreadRoot  = "/vthread"
const VthreadSeq   = "/vthread/seq"   // vtid 序列计数器
const VthreadReady = "/vthread/ready" // 新 vthread 就绪通知队列

// VThread 返回 /vthread/<vtid>
func VThread(vtid string) string { return "/vthread/" + vtid }

// VThreadDone 返回 /vthread/<vtid>/done
func VThreadDone(vtid string) string { return "/vthread/" + vtid + "/done" }

// VThreadTerm 返回 /vthread/<vtid>/term
func VThreadTerm(vtid string) string { return "/vthread/" + vtid + "/term" }

// VThreadAt 返回 /vthread/<vtid>/<key>，运行时局部槽访问的通用路径构造。
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
