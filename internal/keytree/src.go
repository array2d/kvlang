package keytree

import "fmt"

const SrcRoot = "/src"

// SrcFunc 返回 /src/<pkg>/<funcname>，存储函数完整源码文本。
// pkg 为包路径，如 "print"、"builtin/arith"。
func SrcFunc(pkg, name string) string { return "/src/" + pkg + "/" + name }

// SrcFuncLine 返回 /src/<pkg>/<funcname>/<i>（保留，兼容按行索引）。
func SrcFuncLine(pkg, name string, i int) string {
	return fmt.Sprintf("/src/%s/%s/%d", pkg, name, i)
}
