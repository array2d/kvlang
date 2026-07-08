package op

// IsComputeOp 判断是否为计算类算子 (排除控制流和生命周期)。
func IsComputeOp(opcode string) bool {
	switch opcode {
	case OpCall, OpReturn, OpIf, OpFor,
		OpNewTensor, OpDelTensor, OpCloneTensor:
		return false
	}
	return true
}
