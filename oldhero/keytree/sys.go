package keytree

// RwirRoot rwir 签名统一落在 /lib 下（与 rwfunc 同根，靠 kind 区分）。
const RwirRoot = LibRoot

func Rwir(opcode string) string { return RwirRoot + PathSegSep + opcode }
