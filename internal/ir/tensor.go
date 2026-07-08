package ir

const (
	OpNewTensor   = "newtensor"
	OpDelTensor   = "deltensor"
	OpCloneTensor = "clonetensor"
)

// IsLifecycleOp 判断是否为 tensor 生命周期操作。
func IsLifecycleOp(opcode string) bool {
	return opcode == OpNewTensor || opcode == OpDelTensor || opcode == OpCloneTensor
}
