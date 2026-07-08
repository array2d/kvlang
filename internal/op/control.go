package op

const (
	OpCall   = "call"
	OpReturn = "return"
	OpIf     = "if"
	OpFor    = "for"
)

// IsControlOp 判断是否为控制流指令。
func IsControlOp(opcode string) bool {
	switch opcode {
	case OpCall, OpReturn, OpIf, OpFor:
		return true
	}
	return false
}
