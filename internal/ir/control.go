package ir

// IsControlOp 判断是否为控制流指令 (call/return/if/for)。
func IsControlOp(opcode string) bool {
	switch opcode {
	case "call", "return", "if", "for":
		return true
	}
	return false
}
