// Package ast 定义 kvlang 抽象语法树节点类型。
//
// 本包仅包含纯数据结构定义，不涉及文件 I/O、Redis 等外部依赖。
// 如需将 AST 节点持久化到 Redis，请使用 internal/register 包。
package ast

// Func 表示一个函数定义。
type Func struct {
	Name      string   // 函数名
	Signature string   // def funcName(A:int, B:int) -> (C:int)
	Body      []string // kvlang 指令行
}

// Instruction 表示解析后的单条 kvlang 指令。
type Instruction struct {
	Opcode string   // 操作码
	Reads  []string // 输入参数
	Writes []string // 输出槽位
}

// FormalParams 表示函数签名的形参列表。
type FormalParams struct {
	Reads  []string
	Writes []string
}
