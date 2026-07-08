package ir

// IsLifecycleOp 判断是否为 tensor 生命周期操作 (newtensor/deltensor/clonetensor)。
func IsLifecycleOp(opcode string) bool {
	return opcode == "newtensor" || opcode == "deltensor" || opcode == "clonetensor"
}
