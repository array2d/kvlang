package ast

// File 表示一个完整的 .kv 源文件。
type File struct {
	Aliases  map[string]string // import "path" as alias → alias→path (fix-035)
	Package string             // lib 块声明的包名；空 = 匿名（fix-034)
	Funcs         []Func
	TopLevelCalls []*Instruction // def 块外部的顶层调用（Expr=表达式，Writes=输出槽）
	InitBody      []Stmt         // init { ... } 初始化块（fix-036：parseBody 全语法支持）
	Imports       []string       // import 导入列表（fix-033）
}
