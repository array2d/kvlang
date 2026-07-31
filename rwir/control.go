package rwir

const OpCall   = "call"
const OpReturn = "return"
const OpBr     = "br"
const OpGoto   = "goto"

// IsControlOp 判断是否为控制流指令（kvcpu 执行层可见的）。
func IsControlOp(opcode string) bool {
	switch opcode {
	case OpCall, OpReturn, OpBr, OpGoto:
		return true
	}
	return false
}
