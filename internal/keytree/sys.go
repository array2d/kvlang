package keytree

const SysRoot = "/sys"

// SysOp 返回 /sys/op/<backend>/<n>（op 后端第 n 个实例状态）
func SysOp(backend, n string) string { return "/sys/op/" + backend + "/" + n }

// SysOpCmd 返回 /sys/op/<backend>/<n>/cmd（实例命令队列）
func SysOpCmd(backend, n string) string { return "/sys/op/" + backend + "/" + n + "/cmd" }

// SysOpFunc 返回 /sys/op/<backend>/func/<name>（算子函数定义）
func SysOpFunc(backend, name string) string { return "/sys/op/" + backend + "/lib/" + name }

// SysOpRoot 返回 /sys/op（用于 List 枚举所有 backend）
const SysOpRoot = "/sys/op"
