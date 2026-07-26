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

func frameMember(root, seg string) string { return root + MemberSep + seg }

func Stack(root string) string { return root + PathSegSep }
func FrameRO(root string) string    { return frameMember(root, SegRO) }

func RParam(root, name string) string { return frameMember(root, SegRParam) + PathSegSep + name }
func WParam(root, name string) string { return frameMember(root, SegWParam) + PathSegSep + name }

func FrameRoot(pc string) string {
	if idx := strings.LastIndex(pc, PathSegSep+"["); idx >= 0 { return pc[:idx] }
	panic(fmt.Sprintf("FrameRoot: pc has no %s[coord] segment: %q", PathSegSep, pc))
}

func EntryPC(root string) string  { return root + PathSegSep + "[0,0]" }
func IsEntryPC(pc string) bool {
	idx := strings.LastIndex(pc, PathSegSep+"[")
	return idx >= 0 && pc[idx:] == PathSegSep+"[0,0]"
}
