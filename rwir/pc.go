package rwir

import (
	"fmt"
	"strconv"
	"strings"
)

// NextPC 返回当前指令的下一条指令坐标。
func NextPC(pc string) string {
	parts := strings.Split(pc, "/")
	last := parts[len(parts)-1]
	num := ExtractAddr0(last)
	parts[len(parts)-1] = fmt.Sprintf("[%d,0]", num+1)
	return strings.Join(parts, "/")
}

// ExtractAddr0 从 coord 字符串（如 "[3,0]"）提取 s0 坐标。
func ExtractAddr0(coord string) int {
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
