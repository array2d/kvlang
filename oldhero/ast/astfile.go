package ast

// File 表示一个完整的 .kv 源文件。
type File struct {
	Package       string             // lib 块声明的包名；空 = 匿名（fix-034)
	RwirDecls     []RwirDecl         // rwir 声明（签名，无体）
	Funcs         []Func
	TopLevelCalls []*Instruction // rwfunc 块外部的顶层调用
	InitBody      []Stmt         // init { ... } 初始化块（fix-036：parseBody 全语法支持）
}
