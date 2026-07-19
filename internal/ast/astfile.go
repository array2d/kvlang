package ast

// File 表示一个完整的 .kv 源文件。
type File struct {
	Aliases  map[string]string // import "path" as alias → alias→path (fix-035)
	Package string // lib 块声明的包名；空 = 匿名（fix-034）
	Funcs         []Func
	TopLevelCalls []*Instruction // def 块外部的顶层调用（Expr=表达式，Writes=输出槽）
	Imports       []string       // import 导入列表（fix-033）
	ImportPaths   []string       // import "path" 引号文件导入子集（与 Imports 同序；fix-035，loader 用来区分文件 vs kvspace）
}
