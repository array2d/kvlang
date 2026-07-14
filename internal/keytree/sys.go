package keytree

const SysRoot = "/sys"

// SysVM 返回 /sys/vm/<id>
func SysVM(id string) string { return "/sys/vm/" + id }

// SysVMHB 返回 /sys/vm/<id>/hb（心跳）
func SysVMHB(id string) string { return "/sys/vm/" + id + "/hb" }

// SysVMCmd 返回 /sys/vm/<id>/cmd（VM 命令队列）
func SysVMCmd(id string) string { return "/sys/vm/" + id + "/cmd" }

// SysOp 返回 /sys/op/<backend>/<n>（op 后端第 n 个实例状态）
func SysOp(backend, n string) string { return "/sys/op/" + backend + "/" + n }

// SysOpCmd 返回 /sys/op/<backend>/<n>/cmd（实例命令队列）
func SysOpCmd(backend, n string) string { return "/sys/op/" + backend + "/" + n + "/cmd" }

// SysOpFunc 返回 /sys/op/<backend>/func/<name>（算子函数定义）
func SysOpFunc(backend, name string) string { return "/sys/op/" + backend + "/func/" + name }

// SysOpRoot 返回 /sys/op（用于 List 枚举所有 backend）
const SysOpRoot = "/sys/op"
