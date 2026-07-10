package op

const (
	OpCall     = "call"
	OpReturn   = "return"
	OpIf       = "if"
	OpElse     = "else"
	OpFor      = "for"
	OpWhile    = "while"
	OpBreak    = "break"
	OpContinue = "continue"
	OpBr       = "br"
	OpGoto     = "goto"
)

// IsControlOp 判断是否为控制流指令。
func IsControlOp(opcode string) bool {
	switch opcode {
	case OpCall, OpReturn, OpIf, OpFor, OpBr:
		return true
	}
	return false
}
