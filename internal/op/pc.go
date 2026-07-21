package op

import (
	"fmt"
	"strconv"
	"strings"
)

// WriteSlotPC 从指令 opcode 路径推导第 i 个写槽路径（i 从 0 起）。
//
// 调用指令 opcode 存在 [addr0,0]，写槽存在 [addr0,1], [addr0,2], ...
// 例：WriteSlotPC("/vthread/42/.funclib/[3,0]", 0) → "/vthread/42/.funclib/[3,1]"
//
func WriteSlotPC(pc string, i int) string {
	idx := strings.LastIndex(pc, "/[")
	if idx < 0 {
		return pc
	}
	addr0 := extractAddr0(pc[idx+1:])
	return fmt.Sprintf("%s/[%d,%d]", pc[:idx], addr0, i+1)
}

// NextPC 返回当前指令的下一条指令坐标。
func NextPC(pc string) string {
	parts := strings.Split(pc, "/")
	last := parts[len(parts)-1]
	num := extractAddr0(last)
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

// ExtractAddr0FromPC 从含 /[coord] 的绝对路径提取最末段 coord 的 s0 坐标。
func ExtractAddr0FromPC(pc string) int {
	idx := strings.LastIndex(pc, "/[")
	if idx < 0 {
		return 0
	}
	return ExtractAddr0(pc[idx+1:])
}

func extractAddr0(coord string) int { return ExtractAddr0(coord) }
