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

// IsControlOp 判断是否为控制流指令（kvcpu 执行层可见的）。
// OpIf/OpFor/OpWhile 等已在 lower 阶段消除，不在 IsControlOp 中。
func IsControlOp(opcode string) bool {
	switch opcode {
	case OpCall, OpReturn, OpBr, OpGoto:
		return true
	}
	return false
}
