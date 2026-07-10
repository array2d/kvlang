package keytree

import (
	"fmt"
	"strings"
)

const vthreadPrefix = "/vthread/"

// VThread 返回 /vthread/<id>
func VThread(id string) string { return vthreadPrefix + id }

// VThreadAt 返回 /vthread/<id>/<pc>
func VThreadAt(id, pc string) string { return vthreadPrefix + id + "/" + pc }

// VThreadSub 返回 /vthread/<id>/<pc>/  (子栈前缀)
func VThreadSub(id, pc string) string {
	s := vthreadPrefix + id + "/"
	if pc != "" {
		s += pc
	}
	if !strings.HasSuffix(s, "/") {
		s += "/"
	}
	return s
}

// VThreadSlot 返回 /vthread/<id>/[a,b]
func VThreadSlot(id string, a, b int) string { return fmt.Sprintf(vthreadPrefix+"%s/[%d,%d]", id, a, b) }

// VThreadTerm 返回 /vthread/<id>/term
func VThreadTerm(id string) string { return vthreadPrefix + id + "/term" }

// VThreadPattern 返回 /vthread/* (SCAN 用)
func VThreadPattern() string { return vthreadPrefix + "*" }

// Roots 返回所有根路径前缀。
func Roots() []string {
	return []string{"/vthread", "/src", "/func", "/sys", "/op"}
}
