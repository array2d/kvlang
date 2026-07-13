package ast

// File 表示一个完整的 .kv 源文件。
type File struct {
	Funcs         []Func
	TopLevelCalls []*Instruction // def 块外部的顶层调用（Expr=表达式，Writes=输出槽）
}
