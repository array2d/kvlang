package keytree

import "fmt"

const srcPrefix = "/src/func/"

// SrcFunc 返回 /src/func/<name>
func SrcFunc(name string) string { return srcPrefix + name }

// SrcFuncLine 返回 /src/func/<name>/<i>
func SrcFuncLine(name string, i int) string { return fmt.Sprintf(srcPrefix+"%s/%d", name, i) }
