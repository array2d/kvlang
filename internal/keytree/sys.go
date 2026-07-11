package keytree

const SysRoot = "/sys"
const sysPrefix = "/sys/"
// SysVM 返回 /sys/vm/<id>
func SysVM(id string) string { return sysPrefix + "vm/" + id }

// SysHeartbeat 返回 /sys/heartbeat/vm:<id>
func SysHeartbeat(id string) string { return sysPrefix + "heartbeat/vm:" + id }

// SysVtidCounter 返回 /sys/vtid_counter
const SysVtidCounter = sysPrefix + "vtid_counter"

// SysOpPlatRoot 返回 /sys/op-plat
const SysOpPlatRoot = sysPrefix + "op-plat"

// SysOpPlatInst 返回 /sys/op-plat/<instance>，instance 形如 "op-cuda:0"
func SysOpPlatInst(instance string) string { return SysOpPlatRoot + "/" + instance }

// SysHeapPlatRoot 返回 /sys/heap-plat
const SysHeapPlatRoot = sysPrefix + "heap-plat"

// SysHeapPlatInst 返回 /sys/heap-plat/<instance>，instance 形如 "heap-cuda:0"
func SysHeapPlatInst(instance string) string { return SysHeapPlatRoot + "/" + instance }

// CmdQueue 返回后端实例的命令队列 key，形如 cmd:<instance>
// 例: CmdQueue("op-cuda:0") → "cmd:op-cuda:0"
func CmdQueue(instance string) string { return "cmd:" + instance }

// SysTerm 返回 /sys/term/<name>/<stream>
func SysTerm(name, stream string) string { return sysPrefix + "term/" + name + "/" + stream }

// SysCmdVM 返回 /sys/cmd/vm/<id>
func SysCmdVM(id string) string { return sysPrefix + "cmd/vm/" + id }
