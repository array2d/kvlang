package keytree

import (
	"fmt"
	"strings"
)

// ── 帧路径工具 ────────────────────────────────────────────────────
//
// 帧结构（overlay 模型）：
//
//	callPC = parentFrame/[coord]            调用指令 PC = 子帧根
//	frameRoot/.code                         overlay merge 点
//	frameRoot/.upper                        可写层（本帧局部变量、运行时状态）
//	frameRoot/.rootfunc                     入口函数名（TCO 不更新）
//	frameRoot/.rparam/<name>               读参重定向
//	frameRoot/.wparam/<name>               写参重定向
//
// PC 格式（纯 / 层级，frameRoot 通过去掉末尾 /[coord] 得到）：
//
//	/vthread/vtid/[0,0]                    顶层帧第一条指令
//	/vthread/vtid/[3,0]                    顶层帧第四条指令（调用点 = 子帧根）
//	/vthread/vtid/[3,0]/[0,0]              子帧第一条指令
//	/vthread/vtid/[3,0]/[1,0]/[0,0]        孙帧第一条指令

func FuncLib(frameRoot string) string {
	return frameRoot + PathSegSep + ReservedPrefix + SegFunclib
}

func CodeOverlay(frameRoot string) string {
	return frameRoot + PathSegSep + ReservedPrefix + SegCode
}

func CodeUpper(frameRoot string) string {
	return frameRoot + PathSegSep + ReservedPrefix + SegUpper
}

func RootFunc(frameRoot string) string {
	return frameRoot + PathSegSep + ReservedPrefix + SegRootfunc
}

func FrameRO(frameRoot string) string {
	return frameRoot + PathSegSep + ReservedPrefix + SegRO
}

func RParam(frameRoot, name string) string {
	return frameRoot + PathSegSep + ReservedPrefix + SegRParam + PathSegSep + name
}

func WParam(frameRoot, name string) string {
	return frameRoot + PathSegSep + ReservedPrefix + SegWParam + PathSegSep + name
}

func FramePkg(frameRoot string) string {
	return frameRoot + PathSegSep + ReservedPrefix + SegPkg
}

func FrameRoot(pc string) string {
	if idx := strings.LastIndex(pc, PathSegSep+"["); idx >= 0 {
		return pc[:idx]
	}
	panic(fmt.Sprintf("FrameRoot: pc has no %s[coord] segment: %q", PathSegSep, pc))
}

func EntryPC(frameRoot string) string { return frameRoot + PathSegSep + "[0,0]" }

func IsEntryPC(pc string) bool {
	idx := strings.LastIndex(pc, PathSegSep+"[")
	return idx >= 0 && pc[idx:] == PathSegSep+"[0,0]"
}
