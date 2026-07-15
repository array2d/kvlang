package op

const OpCall     = "call"
const OpReturn   = "return"
const OpIf       = "if"
const OpElse     = "else"
const OpFor      = "for"
const OpWhile    = "while"
const OpBreak    = "break"
const OpContinue = "continue"
const OpBr       = "br"
const OpGoto     = "goto"

// IsControlOp 判断是否为控制流指令。
func IsControlOp(opcode string) bool {
	switch opcode {
	case OpCall, OpReturn, OpIf, OpFor, OpBr, OpGoto:
		return true
	}
	return false
}
