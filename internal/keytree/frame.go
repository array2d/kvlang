package keytree

import (
	"fmt"
	"strings"
)

// ── 帧路径工具 ────────────────────────────────────────────────────
//
// merge = FrameRoot, overlay 直接挂在帧根上。resolveOL 自动跳过 ReservedPrefix，
// .upper/.rootfunc 等不会被 overlay 拦截。
//
//	callPC = parentFrame/[coord]            调用指令 PC = 子帧根
//	frameRoot                               overlay merge 点（也是指令取指 keyBase）
//	frameRoot/.upper                        可写层（在 merge 下，resolveOL 跳过）
//	frameRoot<MemberSep>rootfunc            入口函数名（merge 子树外）
//	frameRoot<MemberSep>rparam/<name>       读参重定向
//	frameRoot<MemberSep>wparam/<name>       写参重定向

func frameMember(root, seg string) string { return root + MemberSep + seg }

func CodeUpper(root string) string  { return root + PathSegSep + ReservedPrefix + SegUpper }
func RootFunc(root string) string   { return frameMember(root, SegRootfunc) }
func FrameRO(root string) string    { return frameMember(root, SegRO) }
func FramePkg(root string) string   { return frameMember(root, SegPkg) }

func RParam(root, name string) string { return frameMember(root, SegRParam) + PathSegSep + name }
func WParam(root, name string) string { return frameMember(root, SegWParam) + PathSegSep + name }

func FuncLib(root string) string { return frameMember(root, SegFunclib) }

func FrameRoot(pc string) string {
	if idx := strings.LastIndex(pc, PathSegSep+"["); idx >= 0 { return pc[:idx] }
	panic(fmt.Sprintf("FrameRoot: pc has no %s[coord] segment: %q", PathSegSep, pc))
}

func EntryPC(root string) string  { return root + PathSegSep + "[0,0]" }
func IsEntryPC(pc string) bool {
	idx := strings.LastIndex(pc, PathSegSep+"[")
	return idx >= 0 && pc[idx:] == PathSegSep+"[0,0]"
}
