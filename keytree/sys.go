package keytree

const RwirRoot = PathSegSep + SegRwir

func Rwir(opcode string) string { return RwirRoot + PathSegSep + opcode }

// RwirRuntime 返回 /rwir/{runtime}/{opcode}。{runtime} 反射自可执行文件名。
func RwirRuntime(runtime, opcode string) string {
	return RwirRoot + PathSegSep + runtime + PathSegSep + opcode
}
