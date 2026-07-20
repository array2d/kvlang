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
//   frameRoot + "/.fn"           软链接 → /lib/<pkg>/<name>（只读指令区）→ FnCode(frameRoot)
//   frameRoot + "/<param>"       参数（本帧局部变量）
//   frameRoot + "/.callpc"        返回地址（仅子帧）→ CallPC(frameRoot)
//   frameRoot + "/.rootfunc"      根函数名（TCO 不更新）→ RootFunc(frameRoot)
//   frameRoot + "/.ro"            只读参数名单 → FrameRO(frameRoot)
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

// FrameRO 返回帧的只读参数名单键：frameRoot + "/.ro"（fix-027 读参写保护，
// Bootstrap/HandleCall 绑定参数时写入逗号分隔名单，kvcpu 写槽检查用）。
func FrameRO(frameRoot string) string { return frameRoot + "/.ro" }

// CallPC 返回帧的返回地址键：frameRoot + "/.callpc"。
// 存储调用指令的绝对路径，HandleReturn 据此推算写槽路径并恢复父 PC。
// 顶层帧（Bootstrap 创建）无此键。
func CallPC(frameRoot string) string { return frameRoot + "/.callpc" }

// RootFunc 返回帧的根函数名键：frameRoot + "/.rootfunc"。
// TCO 复用帧时不更新此键（保持根函数名），供 resolveLabel 裸标签解析。
func RootFunc(frameRoot string) string { return frameRoot + "/.rootfunc" }

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
