package ast

// File 表示一个完整的 .kv 源文件。
type File struct {
	Funcs         []Func
	TopLevelCalls []TopLevelCall
	PreambleLines []string // def 块外部的原始 kvlang 指令行（含引号），用于合成 pre_main
}

// TopLevelCall 表示 def 块外部的顶层调用表达式。
type TopLevelCall struct {
	FuncName string   // 函数名
	Args     []string // 实参
	Outputs  []string // 输出槽位
}
