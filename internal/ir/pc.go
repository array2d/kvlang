package ir

import (
	"fmt"
	"strconv"
	"strings"
)

// NextPC 返回当前指令的下一条指令坐标。
func NextPC(pc string) string {
	parts := strings.Split(pc, "/")
	last := parts[len(parts)-1]
	num := extractAddr0(last)
	parts[len(parts)-1] = fmt.Sprintf("[%d,0]", num+1)
	return strings.Join(parts, "/")
}

// ParentPC 返回子栈调用者的下一条指令坐标。
func ParentPC(pc string) string {
	idx := strings.LastIndex(pc, "/")
	if idx < 0 {
		return pc
	}
	return NextPC(pc[:idx])
}

// parsePC 解析 PC 字符串，返回前缀和 addr0。
func parsePC(pc string) (prefix string, addr0 int) {
	idx := strings.LastIndex(pc, "/")
	if idx >= 0 {
		prefix = pc[:idx+1]
		addr0 = extractAddr0(pc[idx+1:])
	} else {
		addr0 = extractAddr0(pc)
	}
	return
}

func extractAddr0(coord string) int {
	s := strings.Trim(coord, "[]")
	parts := strings.Split(s, ",")
	if len(parts) > 0 {
		n, err := strconv.Atoi(strings.TrimSpace(parts[0]))
		if err != nil {
			return 0
		}
		return n
	}
	return 0
}
