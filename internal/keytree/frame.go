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
//   callPC + "/_fn"             软链接 → /func/<pkg>/<name>（只读指令区）
//   callPC + "/<param>"         参数（本帧局部变量）
//   callPC + "/.callpc"         存储 callPC 自身，供 HandleReturn 恢复
//   callPC + "/.ret0"           调用方的写槽（return 时写入父帧）
//
// 层级嵌套（路径深度 = 调用栈深度）：
//
//   /vthread/42                         L0 vthread 根
//   /vthread/42/_fn/[i,j]              main 的指令（通过 L0 链接读取）
//   /vthread/42/[3,0]                   add 的帧根（callPC = /vthread/42/_fn/[3,0]）
//   /vthread/42/[3,0]/_fn/[i,j]        add 的指令
//   /vthread/42/[3,0]/[2,0]             sub 的帧根
//   /vthread/42/[3,0]/[2,0]/_fn/[i,j]  sub 的指令

// FnCode 返回帧的指令链接路径：frameRoot + "/_fn"
func FnCode(frameRoot string) string { return frameRoot + "/_fn" }

// FrameRoot 从任意指令绝对 PC 提取帧根（即 callPC）。
//
//	/vthread/42/_fn/[2,0]           → /vthread/42          (main 的帧根)
//	/vthread/42/[3,0]/_fn/[0,0]    → /vthread/42/[3,0]    (add 的帧根)
//	/vthread/42/[3,0]/[2,0]/_fn/[1,0] → /vthread/42/[3,0]/[2,0]
// FrameRoot 从任意指令绝对 PC 提取帧根（即 callPC）。
// 所有合法执行 PC 均由 Bootstrap / HandleCall 产生，格式保证含 "/_fn/"。
//
//	/vthread/42/_fn/[2,0]           → /vthread/42
//	/vthread/42/[3,0]/_fn/[0,0]    → /vthread/42/[3,0]
func FrameRoot(pc string) string {
	if idx := strings.LastIndex(pc, "/_fn/"); idx >= 0 {
		return pc[:idx]
	}
	panic(fmt.Sprintf("FrameRoot: pc has no /_fn/ segment: %q", pc))
}

// ChildFrameRoot 从 callPC 推导被调方帧根。
//
//   callPC = parentFrameRoot + "/_fn/" + "[coord]"
//   childFrameRoot = parentFrameRoot + "/" + "[coord]"
//
// 对顶层调用（callPC = /vthread/vtid/_fn/[0,0]，无嵌套 /_fn/）：
//   parentFrameRoot = /vthread/vtid（vthread 根即 pre_main 帧根）
//   childFrameRoot = parentFrameRoot + "/" + "[coord]" = /vthread/vtid/[0,0]
// ChildFrameRoot 从 callPC 推导被调方帧根。
//
//   callPC = parentFrameRoot + "/_fn/" + "[coord]"
//   childFrameRoot = parentFrameRoot + "/" + "[coord]"
//
// 例：/vthread/42/_fn/[3,0] → /vthread/42/[3,0]
func ChildFrameRoot(callPC string) string {
	idx := strings.LastIndex(callPC, "/_fn/")
	if idx < 0 {
		panic(fmt.Sprintf("ChildFrameRoot: callPC has no /_fn/ segment: %q", callPC))
	}
	// callPC[:idx] = parentFrameRoot, callPC[idx+5:] = "[coord]"
	return callPC[:idx] + "/" + callPC[idx+5:]
}
