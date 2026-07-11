package keytree

const SrcRoot = "/src"

// SrcFunc 返回 /src/<pkg>/<funcname>，存储函数完整源码文本。
// pkg 为包路径，如 "print"、"builtin/arith"。
func SrcFunc(pkg, name string) string { return "/src/" + pkg + "/" + name }
