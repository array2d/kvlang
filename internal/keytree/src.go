package keytree

import "fmt"

// SrcFunc 返回 /src/func/<name>
func SrcFunc(name string) string { return "/src/func/" + name }

// SrcFuncLine 返回 /src/func/<name>/<i>
func SrcFuncLine(name string, i int) string { return fmt.Sprintf("/src/func/%s/%d", name, i) }
