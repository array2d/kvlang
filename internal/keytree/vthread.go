package keytree

import (
	"fmt"
	"strings"
)

// ── vthread 路径工具 ────────────────────────────────────────────────

const VthreadRoot = PathSegSep + PathSegVthread
const VthreadSeq  = PathSegSep + PathSegVthread + PathSegSep + SegSeq

func VThread(vtid string) string { return VthreadRoot + PathSegSep + vtid }

func VThreadTerm(vtid string) string { return VthreadRoot + PathSegSep + vtid + PathSegSep + SegTerm }

// ── 引擎保留字段（ReservedPrefix 开头）───────────────────────────────

func VThreadPC(vtid string) string {
	return VthreadRoot + PathSegSep + vtid + PathSegSep + ReservedPrefix + SegPC
}

func VThreadStatus(vtid string) string {
	return VthreadRoot + PathSegSep + vtid + PathSegSep + ReservedPrefix + SegStatus
}

func VThreadCtime(vtid string) string {
	return VthreadRoot + PathSegSep + vtid + PathSegSep + ReservedPrefix + SegCtime
}

func VThreadStatusMsg(vtid, statusVal string) string {
	return VthreadRoot + PathSegSep + vtid + PathSegSep + ReservedPrefix + statusVal + PathSegSep + SegMsg
}

func VThreadDebugger(vtid string) string {
	return VthreadRoot + PathSegSep + vtid + PathSegSep + ReservedPrefix + SegDebugger
}

func VThreadDebuggerPause(vtid string) string {
	return VthreadRoot + PathSegSep + vtid + PathSegSep + ReservedPrefix + SegDebugger + MemberSep + SegPause
}

func VThreadDebuggerResume(vtid string) string {
	return VthreadRoot + PathSegSep + vtid + PathSegSep + ReservedPrefix + SegDebugger + MemberSep + SegResume
}

func VThreadAt(vtid, key string) string { return VthreadRoot + PathSegSep + vtid + PathSegSep + key }

// ── PC 解析 ─────────────────────────────────────────────────────────

func VtidFromPC(pc string) string {
	const pfx = PathSegSep + PathSegVthread + PathSegSep
	if !strings.HasPrefix(pc, pfx) { return "" }
	rest := pc[len(pfx):]
	if slash := strings.Index(rest, PathSegSep); slash >= 0 { return rest[:slash] }
	return rest
}

func VThreadSlot(vtid, frame string, i, j int) string {
	if frame == "" {
		return fmt.Sprintf(VthreadRoot+PathSegSep+"%s/[%d,%d]", vtid, i, j)
	}
	return fmt.Sprintf(VthreadRoot+PathSegSep+"%s/%s/[%d,%d]", vtid, frame, i, j)
}

func VThreadFrame(vtid, frame string) string {
	if frame == "" { return VthreadRoot + PathSegSep + vtid }
	return VthreadRoot + PathSegSep + vtid + PathSegSep + frame
}
