package ir

// IsComputeOp 判断是否为计算类算子 (排除控制流和生命周期)。
func IsComputeOp(opcode string) bool {
	switch opcode {
	case "call", "return", "if", "for",
		"newtensor", "deltensor", "clonetensor":
		return false
	}
	return true
}
