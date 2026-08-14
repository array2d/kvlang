package keytree

const SysRoot = PathSegSep + SegSys

func SysOp(backend, n string) string { return SysRoot + PathSegSep + SegOp + PathSegSep + backend + PathSegSep + n }

func SysOpCmd(backend, n string) string { return SysOp(backend, n) + PathSegSep + SegCmd }

func SysOpFunc(backend, name string) string { return SysRoot + PathSegSep + SegOp + PathSegSep + backend + PathSegSep + SegFunc + PathSegSep + name }

const SysOpRoot   = PathSegSep + SegSys + PathSegSep + SegOp
const SysRwirRoot = PathSegSep + SegSys + PathSegSep + SegRwir

func SysRwir(opcode string) string { return SysRwirRoot + PathSegSep + opcode }

// SysRwirRuntime 返回 /sys/rwir/{runtime}/{opcode}。{runtime} 反射自可执行文件名。
func SysRwirRuntime(runtime, opcode string) string {
	return SysRwirRoot + PathSegSep + runtime + PathSegSep + opcode
}
