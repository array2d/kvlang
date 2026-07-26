package keytree

import (
	"fmt"
	"strings"
)

// ── vthread 路径工具 ────────────────────────────────────────────────

const VthreadRoot = PathSegSep + PathSegVthread
const VthreadSeq  = VthreadRoot + PathSegSep + SegSeq

func VThread(vtid string) string { return VthreadRoot + PathSegSep + vtid }

func vtMember(vtid, seg string) string { return VThread(vtid) + MemberSep + seg }

func VThreadPC(vtid string) string              { return vtMember(vtid, SegPC) }
func VThreadStatus(vtid string) string          { return vtMember(vtid, SegStatus) }
func VThreadCtime(vtid string) string           { return vtMember(vtid, SegCtime) }
func VThreadDebugger(vtid string) string        { return vtMember(vtid, SegDebugger) }
func VThreadTerm(vtid string) string            { return vtMember(vtid, SegTerm) }

func VThreadStatusMsg(vtid, statusVal string) string {
	return vtMember(vtid, statusVal) + PathSegSep + SegMsg
}

func VThreadDebuggerPause(vtid string) string {
	return vtMember(vtid, SegDebugger) + MemberSep + SegPause
}

func VThreadDebuggerResume(vtid string) string {
	return vtMember(vtid, SegDebugger) + MemberSep + SegResume
}

// VThreadAt 返回 /vthread/<vtid>/<key>，通用路径构造。
func VThreadAt(vtid, key string) string { return VThread(vtid) + PathSegSep + key }

// ── PC 解析 ─────────────────────────────────────────────────────────

func VtidFromPC(pc string) string {
	pfx := VthreadRoot + PathSegSep
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
	if frame == "" { return VThread(vtid) }
	return VThread(vtid) + PathSegSep + frame
}
