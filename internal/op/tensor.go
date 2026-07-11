package op

// tensor.* 生命周期操作码常量。
// 格式遵循 vtype 命名空间约定：<vtype>.<op>
const (
	OpTensorNew   = "tensor.new"
	OpTensorDel   = "tensor.del"
	OpTensorClone = "tensor.clone"
)

// IsTensorLifecycle 判断是否为 tensor 生命周期操作。
func IsTensorLifecycle(opcode string) bool {
	return opcode == OpTensorNew || opcode == OpTensorDel || opcode == OpTensorClone
}
