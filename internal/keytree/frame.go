package keytree

import (
	"fmt"
	"strings"
)

// ── 帧路径工具 ────────────────────────────────────────────────────
//
// 链接帧结构（P4 Link 模型）：
//
//   callPC                      调用指令绝对路径（帧唯一标识符）
//   callPC + "/.fn"            软链接 → /func/<pkg>/<name>（只读指令区）
//   callPC + "/<param>"         参数（本帧局部变量）
//   callPC + "/.callpc"         存储 callPC 自身，供 HandleReturn 恢复
//
// 层级嵌套（路径深度 = 调用栈深度）：
//
//   /vthread/42                           L0 vthread 根
//   /vthread/42/.fn/[i,j]               main 的指令（通过 L0 链接读取）
//   /vthread/42/[3,0]                     add 的帧根（callPC = /vthread/42/.fn/[3,0]）
//   /vthread/42/[3,0]/.fn/[i,j]         add 的指令
//   /vthread/42/[3,0]/[2,0]               sub 的帧根
//   /vthread/42/[3,0]/[2,0]/.fn/[i,j]   sub 的指令

// FnCode 返回帧的指令链接路径：frameRoot + "/.fn"
//
// ".fn" 以 "." 开头，遵循引擎保留键的统一约定（用户代码禁止写 "." 前缀键）。
func FnCode(frameRoot string) string { return frameRoot + "/.fn" }

// FrameRoot 从任意指令绝对 PC 提取帧根（即 callPC）。
//
// 所有合法执行 PC 均由 Bootstrap / HandleCall 产生，格式保证含 "/.fn/"。
//
//	/vthread/42/.fn/[2,0]              → /vthread/42
//	/vthread/42/[3,0]/.fn/[0,0]       → /vthread/42/[3,0]
//	/vthread/42/[3,0]/[2,0]/.fn/[1,0] → /vthread/42/[3,0]/[2,0]
func FrameRoot(pc string) string {
	if idx := strings.LastIndex(pc, "/.fn/"); idx >= 0 {
		return pc[:idx]
	}
	panic(fmt.Sprintf("FrameRoot: pc has no /.fn/ segment: %q", pc))
}

// ChildFrameRoot 从 callPC 推导被调方帧根。
//
//   callPC = parentFrameRoot + "/.fn/" + "[coord]"
//   childFrameRoot = parentFrameRoot + "/" + "[coord]"
//
// 对顶层调用（callPC = /vthread/vtid/.fn/[0,0]）：
//   parentFrameRoot = /vthread/vtid
//   childFrameRoot  = /vthread/vtid/[0,0]
//
// 例：/vthread/42/.fn/[3,0] → /vthread/42/[3,0]
func ChildFrameRoot(callPC string) string {
	idx := strings.LastIndex(callPC, "/.fn/")
	if idx < 0 {
		panic(fmt.Sprintf("ChildFrameRoot: callPC has no /.fn/ segment: %q", callPC))
	}
	// callPC[:idx] = parentFrameRoot, callPC[idx+5:] = "[coord]"
	// "/.fn/" 长度 = 5（注意：之前的 "/._fn/" 是 6，已更名为 "/.fn/"）
	return callPC[:idx] + "/" + callPC[idx+5:]
}
