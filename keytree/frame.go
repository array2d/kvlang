package keytree

import (
	"fmt"
	"strings"
)

// ── 帧路径工具 ────────────────────────────────────────────────────
//
// 帧根本身是 extindex：frameRoot/ extindex → /lib/<pkg>.<name>/
// 局部变量直接存在 frameRoot/var，先查本地层再回落 extindex 目标。
//
//	callPC = parentFrame/[coord]            调用指令 PC = 子帧根
//	frameRoot<MemberSep>rparam/<name>       读参重定向
//	frameRoot<MemberSep>wparam/<name>       写参重定向

func frameMember(root, seg string) string { return Stack(root) + ReservedPrefix + seg }

func Stack(root string) string { return strings.TrimRight(root, PathSegSep) + PathSegSep }
func FrameRO(root string) string    { return frameMember(root, SegRO) }

func RParam(root, name string) string { return frameMember(root, SegRParam) + PathSegSep + name }
func WParam(root, name string) string { return frameMember(root, SegWParam) + PathSegSep + name }

func CallPC(root string) string   { return frameMember(root, SegCallPC) }
func ReturnPC(root string) string { return frameMember(root, SegReturnPC) }

// ParentFrame 返回父帧根。label 帧返回上级目录。
// 帧类型判定不靠路径模式——读帧根 extindex target 的 XValue.Kind()（rwfunc/label）。
func ParentFrame(frameRoot string) string {
	trimmed := strings.TrimRight(frameRoot, PathSegSep)
	lastSep := strings.LastIndex(trimmed, PathSegSep)
	if lastSep < 0 { return "" }
	return trimmed[:lastSep] + PathSegSep
}

func FrameRoot(pc string) string {
	if idx := strings.LastIndex(pc, PathSegSep+"["); idx >= 0 { return pc[:idx] }
	panic(fmt.Sprintf("FrameRoot: pc has no %s[coord] segment: %q", PathSegSep, pc))
}

func EntryPC(root string) string {
	root = strings.TrimRight(root, PathSegSep)
	return root + PathSegSep + "[0,0]"
}
func IsEntryPC(pc string) bool {
	idx := strings.LastIndex(pc, PathSegSep+"[")
	return idx >= 0 && pc[idx:] == PathSegSep+"[0,0]"
}
