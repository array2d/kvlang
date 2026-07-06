package ir

// IsControlOp 判断是否为控制流指令 (call/return/if/for)。
func IsControlOp(opcode string) bool {
	switch opcode {
	case "call", "return", "if", "for":
		return true
	}
	return false
}

// IsLifecycleOp 判断是否为 tensor 生命周期操作 (newtensor/deltensor/clonetensor)。
func IsLifecycleOp(opcode string) bool {
	return opcode == "newtensor" || opcode == "deltensor" || opcode == "clonetensor"
}

// IsComputeOp 判断是否为计算类算子 (非控制流、非生命周期)。
func IsComputeOp(opcode string) bool {
	return !isLifecycleOrControl(opcode)
}

func isLifecycleOrControl(opcode string) bool {
	switch opcode {
	case "call", "return", "if", "for",
		"newtensor", "deltensor", "clonetensor":
		return true
	}
	return false
}
