package keytree

// SysVM 返回 /sys/vm/<id>
func SysVM(id string) string { return "/sys/vm/" + id }

// SysHeartbeat 返回 /sys/heartbeat/vm:<id>
func SysHeartbeat(id string) string { return "/sys/heartbeat/vm:" + id }

// SysVtidCounter 返回 /sys/vtid_counter
const SysVtidCounter = "/sys/vtid_counter"

// SysOpPlatPattern 返回 /sys/op-plat/* (SCAN 用)
func SysOpPlatPattern() string { return "/sys/op-plat/*" }

// SysTerm 返回 /sys/term/<name>/<stream>
func SysTerm(name, stream string) string { return "/sys/term/" + name + "/" + stream }

// SysCmdVM 返回 sys:cmd:vm:<id>
func SysCmdVM(id string) string { return "sys:cmd:vm:" + id }
