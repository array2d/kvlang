package keytree

// RwirRoot rwir 签名统一落在 /lib 下（与 rwfunc 同根，靠 kind 区分）。
const RwirRoot = LibRoot

func Rwir(opcode string) string { return RwirRoot + PathSegSep + opcode }

// RwirRuntime 返回 /lib/{runtime}/{opcode}。{runtime} 反射自可执行文件名。
func RwirRuntime(runtime, opcode string) string {
	return RwirRoot + PathSegSep + runtime + PathSegSep + opcode
}
