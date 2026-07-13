package ast

// File 表示一个完整的 .kv 源文件。
type File struct {
	Funcs         []Func
	TopLevelCalls []*Instruction // def 块外部的顶层调用（Opcode=函数名，Reads=实参，Writes=输出槽）
}

