package keytree

import (
	"fmt"
	"strings"
)

// ── 帧路径工具 ────────────────────────────────────────────────────
//
// 链接帧结构（P4 Link 模型）：
//
//   callPC                           调用指令绝对路径（帧唯一标识符）
//   frameRoot + "/.funclib"          软链接 → /lib/<pkg>/<name>（只读指令区）→ FuncLib(frameRoot)
//   frameRoot + "/<param>"           参数（本帧局部变量）
//   frameRoot + "/.callpc"           返回地址（仅子帧）→ CallPC(frameRoot)
//   frameRoot + "/.rootfunc"         根函数名（TCO 不更新）→ RootFunc(frameRoot)
//   frameRoot + "/.ro"               只读参数名单 → FrameRO(frameRoot)
//   frameRoot + "/.rparam/<name>"    读参重定向：绝对路径 → 调用方值位置（零拷贝读）
//   frameRoot + "/.wparam/<name>"    写参重定向：绝对路径 → 调用方写目标（零拷贝写）
//
// 层级嵌套（路径深度 = 调用栈深度）：
//
//   /vthread/42                                  L0 vthread 根
//   /vthread/42/.funclib/[i,j]                   main 的指令（通过 L0 链接读取）
//   /vthread/42/[3,0]                            add 的帧根（callPC = /vthread/42/.funclib/[3,0]）
//   /vthread/42/[3,0]/.funclib/[i,j]             add 的指令
//   /vthread/42/[3,0]/[2,0]                      sub 的帧根
//   /vthread/42/[3,0]/[2,0]/.funclib/[i,j]       sub 的指令

// FuncLib 返回帧的函数库链接路径：frameRoot + "/.funclib"
// 软链接指向 /lib/<pkg>/<name> 只读指令树，PC 中以此分隔符定位帧边界。
func FuncLib(frameRoot string) string { return frameRoot + "/.funclib" }

// FrameRO 返回帧的只读参数名单键：frameRoot + "/.ro"（fix-027 读参写保护，
// Bootstrap/HandleCall 绑定参数时写入逗号分隔名单，kvcpu 写槽检查用）。
func FrameRO(frameRoot string) string { return frameRoot + "/.ro" }

// RParam 返回读参重定向键：frameRoot + "/.rparam/<name>"。
// 存调用方值的绝对路径，CPU 读参时直接从此路径读取，零拷贝。
func RParam(frameRoot, name string) string { return frameRoot + "/.rparam/" + name }

// WParam 返回写参重定向键：frameRoot + "/.wparam/<name>"。
// 存调用方写目标的绝对路径，CPU 写参时直接写入此路径，HandleReturn 不再搬运。
func WParam(frameRoot, name string) string { return frameRoot + "/.wparam/" + name }

// CallPC 返回帧的返回地址键：frameRoot + "/.callpc"。
// 存储调用指令的绝对路径，HandleReturn 据此推算写槽路径并恢复父 PC。
// 顶层帧（Bootstrap 创建）无此键。
func CallPC(frameRoot string) string { return frameRoot + "/.callpc" }

// RootFunc 返回帧的根函数名键：frameRoot + "/.rootfunc"。
// TCO 复用帧时不更新此键（保持根函数名），供 resolveLabel 裸标签解析。
func RootFunc(frameRoot string) string { return frameRoot + "/.rootfunc" }

// FrameRoot 从任意指令绝对 PC 提取帧根（即 callPC）。
//
// 所有合法执行 PC 均由 Bootstrap / HandleCall 产生，格式保证含 "/.funclib/"。
//
//	/vthread/42/.funclib/[2,0]              → /vthread/42
//	/vthread/42/[3,0]/.funclib/[0,0]       → /vthread/42/[3,0]
//	/vthread/42/[3,0]/[2,0]/.funclib/[1,0] → /vthread/42/[3,0]/[2,0]
func FrameRoot(pc string) string {
	const sep = "/.funclib/"
	if idx := strings.LastIndex(pc, sep); idx >= 0 {
		return pc[:idx]
	}
	panic(fmt.Sprintf("FrameRoot: pc has no %s segment: %q", sep, pc))
}

// ChildFrameRoot 从 callPC 推导被调方帧根。
//
//	callPC = parentFrameRoot + "/.funclib/" + "[coord]"
//	childFrameRoot = parentFrameRoot + "/" + "[coord]"
//
// 对顶层调用（callPC = /vthread/vtid/.funclib/[0,0]）：
//
//	parentFrameRoot = /vthread/vtid
//	childFrameRoot  = /vthread/vtid/[0,0]
//
// 例：/vthread/42/.funclib/[3,0] → /vthread/42/[3,0]
func ChildFrameRoot(callPC string) string {
	const sep = "/.funclib/"
	idx := strings.LastIndex(callPC, sep)
	if idx < 0 {
		panic(fmt.Sprintf("ChildFrameRoot: callPC has no %s segment: %q", sep, callPC))
	}
	// callPC[:idx] = parentFrameRoot, callPC[idx+len(sep):] = "[coord]"
	return callPC[:idx] + "/" + callPC[idx+len(sep):]
}
