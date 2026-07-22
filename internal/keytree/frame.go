package keytree

import (
	"fmt"
	"strings"
)

// ── 帧路径工具 ────────────────────────────────────────────────────
//
// 帧结构（P4 Link 模型）：
//
//   callPC                           调用指令绝对路径（帧唯一标识符 = 子帧根）
//   frameRoot/.funclib               软链接 → /lib/<pkg>/<name>（只读指令区）→ FuncLib(frameRoot)
//   frameRoot/<param>                参数（本帧局部变量）
//   frameRoot/.rootfunc              入口函数名（TCO 不更新）→ RootFunc(frameRoot)
//   frameRoot/.ro                    只读参数名单 → FrameRO(frameRoot)
//   frameRoot/.rparam/<name>         读参重定向：调用方值的绝对路径
//   frameRoot/.wparam/<name>         写参重定向：调用方写目标的绝对路径
//
// PC 格式（纯 / 层级，frameRoot 通过去掉末尾 /[coord] 得到）：
//
//   /vthread/vtid/[0,0]                     顶层帧第一条指令
//   /vthread/vtid/[3,0]                     顶层帧第四条指令（调用点 = 子帧根）
//   /vthread/vtid/[3,0]/[0,0]               子帧第一条指令
//   /vthread/vtid/[3,0]/[1,0]/[0,0]         孙帧第一条指令

// FuncLib 返回帧的函数库链接路径（已废弃，保留兼容）。
func FuncLib(frameRoot string) string { return frameRoot + "/" + ReservedPrefix + "funclib" }

// CodeOverlay 返回 overlay merge 点：Overlay(merge=CodeOverlay, lower=funcKey, upper=CodeUpper)。
// kvspace 自动先查 upper 再回落 lower。
func CodeOverlay(frameRoot string) string { return frameRoot + "/" + ReservedPrefix + "code" }

// CodeUpper 返回 overlay 的 upper 层，本帧局部变量写入此路径下。
func CodeUpper(frameRoot string) string { return frameRoot + "/" + ReservedPrefix + "local" }

// RootFunc 返回帧的入口函数名键：frameRoot + "/.rootfunc"。
// TCO 复用帧时不更新此键（保持入口函数名），供 resolveLabel 裸标签解析。
func RootFunc(frameRoot string) string { return frameRoot + "/" + ReservedPrefix + "rootfunc" }

// FrameRO 返回帧的只读参数名单键：frameRoot + "/.ro"。
func FrameRO(frameRoot string) string { return frameRoot + "/" + ReservedPrefix + "ro" }

// RParam 返回读参重定向键：frameRoot + "/.rparam/<name>"。
func RParam(frameRoot, name string) string { return frameRoot + "/" + ReservedPrefix + "rparam/" + name }

// WParam 返回写参重定向键：frameRoot + "/.wparam/<name>"。
func WParam(frameRoot, name string) string { return frameRoot + "/" + ReservedPrefix + "wparam/" + name }

// FramePkg 返回帧的包路径键：frameRoot + "/.pkg"。
// 匿名 lib 时值为空字符串。
func FramePkg(frameRoot string) string { return frameRoot + "/" + ReservedPrefix + "pkg" }

// FrameRoot 从 PC 提取帧根：去掉末尾 /[coord]。
//
//	/vthread/vtid/[3,0]         → /vthread/vtid
//	/vthread/vtid/[3,0]/[1,0]   → /vthread/vtid/[3,0]
func FrameRoot(pc string) string {
	if idx := strings.LastIndex(pc, "/["); idx >= 0 {
		return pc[:idx]
	}
	panic(fmt.Sprintf("FrameRoot: pc has no /[coord] segment: %q", pc))
}

// EntryPC 返回帧的第一条指令 PC：frameRoot + "/[0,0]"。
func EntryPC(frameRoot string) string { return frameRoot + "/[0,0]" }

// IsEntryPC 判断 pc 是否为帧入口指令（最右坐标 = [0,0]）。
func IsEntryPC(pc string) bool {
	idx := strings.LastIndex(pc, "/[")
	return idx >= 0 && pc[idx:] == "/[0,0]"
}
