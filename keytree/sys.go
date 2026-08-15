package keytree

const SysRoot = PathSegSep + SegSys

func SysOp(backend, n string) string { return SysRoot + PathSegSep + SegOp + PathSegSep + backend + PathSegSep + n }

func SysOpCmd(backend, n string) string { return SysOp(backend, n) + PathSegSep + SegCmd }

func SysOpFunc(backend, name string) string { return SysRoot + PathSegSep + SegOp + PathSegSep + backend + PathSegSep + SegFunc + PathSegSep + name }

const SysOpRoot   = PathSegSep + SegSys + PathSegSep + SegOp
const RwirRoot    = PathSegSep + SegRwir

func Rwir(opcode string) string { return RwirRoot + PathSegSep + opcode }

// RwirRuntime 返回 /rwir/{runtime}/{opcode}。{runtime} 反射自可执行文件名。
func RwirRuntime(runtime, opcode string) string {
	return RwirRoot + PathSegSep + runtime + PathSegSep + opcode
}
