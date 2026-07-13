// Package ast 定义 kvlang 抽象语法树节点类型。
// 纯数据结构包，不依赖存储层。
package ast

import (
	"fmt"
	"strings"
)

// Stmt 表示函数体中的一条语句。
type Stmt interface {
	stmt()
	String() string
	FirstLine() string // 返回源码首行，用于 inBody 匹配
}

// Func 表示一个函数定义。
type Func struct {
	Name      string // 函数名
	Signature string // def funcName(A:int, B:int) -> (C:int)
	Body      []Stmt // 函数体语句
}

// FullText 返回函数的完整源码文本（签名 + 函数体），用于存入 /src/<pkg>/<name>。
func (fn *Func) FullText() string {
	var sb strings.Builder
	sb.WriteString(fn.Signature)
	sb.WriteString(" {\n")
	for _, st := range fn.Body {
		sb.WriteString("    ")
		sb.WriteString(st.String())
		sb.WriteString("\n")
	}
	sb.WriteString("}")
	return sb.String()
}

// Instruction 表示一条 kvlang 指令。
type Instruction struct {
	Opcode string   // 操作码
	Reads  []string // 输入参数
	Writes []string // 输出槽位
}

// infixOps 中缀符号算子集合，仅用于 String() 的输出格式化。
var infixOps = map[string]bool{
	"+": true, "-": true, "*": true, "/": true, "%": true,
	"==": true, "!=": true, "<": true, ">": true, "<=": true, ">=": true,
	"&&": true, "||": true, "!": true,
	"&": true, "|": true, "^": true, "<<": true, ">>": true,
}

func (*Instruction) stmt() {}
func (i *Instruction) FirstLine() string { return i.String() }
func (i *Instruction) String() string {
	// 终止指令：br/jump/return 统一用前缀格式
	if i.Opcode == "br" || i.Opcode == "goto" || i.Opcode == "return" {
		if len(i.Reads) > 0 {
			return i.Opcode + "(" + join(i.Reads) + ")"
		}
		return i.Opcode
	}
	// 中缀符号算子：输出中缀形式 X + Y -> Z
	if infixOps[i.Opcode] {
		s := ""
		if len(i.Reads) >= 2 {
			s = i.Reads[0] + " " + i.Opcode + " " + i.Reads[1]
		} else if len(i.Reads) == 1 {
			s = i.Opcode + i.Reads[0]
		} else {
			s = i.Opcode
		}
		if len(i.Writes) > 0 {
			s += " -> " + join(i.Writes)
		}
		return s
	}
	// 命名算子：前缀格式 op(A, B) -> C
	s := i.Opcode
	if len(i.Reads) > 0 {
		s += "(" + join(i.Reads) + ")"
	}
	if len(i.Writes) > 0 {
		s += " -> " + join(i.Writes)
	}
	return s
}

// IfStmt 表示 if/else 控制流。
type IfStmt struct {
	Cond string // 条件表达式
	Then []Stmt // then 分支
	Else []Stmt // else 分支 (可为空)
}

func (*IfStmt) stmt() {}
func (*IfStmt) FirstLine() string { return "if" }
func (s *IfStmt) String() string {
	r := "if (" + s.Cond + ") {\n"
	for _, st := range s.Then { r += "\t" + st.String() + "\n" }
	r += "}"
	if len(s.Else) > 0 {
		r += " else {\n"
		for _, st := range s.Else { r += "\t" + st.String() + "\n" }
		r += "}"
	}
	return r
}

// ForStmt 表示 for 循环：迭代 kvspace 路径上的数据。
type ForStmt struct {
	Var  string // 迭代变量
	Iter string // 迭代源路径，如 './data', '/tensor/x'
	Body []Stmt // 循环体
}

func (*ForStmt) stmt() {}
func (*ForStmt) FirstLine() string { return "for" }
func (s *ForStmt) String() string {
	r := fmt.Sprintf("for (%s in %s) {\n", s.Var, s.Iter)
	for _, st := range s.Body { r += "\t" + st.String() + "\n" }
	return r + "}"
}

// WhileStmt 表示 while 循环。
type WhileStmt struct {
	Cond string // 条件表达式
	Body []Stmt // 循环体
}

func (*WhileStmt) stmt() {}
func (*WhileStmt) FirstLine() string { return "while" }
func (s *WhileStmt) String() string {
	r := "while (" + s.Cond + ") {\n"
	for _, st := range s.Body { r += "\t" + st.String() + "\n" }
	return r + "}"
}

// BreakStmt 表示跳出当前循环。
type BreakStmt struct{}

func (*BreakStmt) stmt()              {}
func (*BreakStmt) FirstLine() string  { return "break" }
func (*BreakStmt) String() string     { return "break" }

// ContinueStmt 表示跳过本次迭代。
type ContinueStmt struct{}

func (*ContinueStmt) stmt()              {}
func (*ContinueStmt) FirstLine() string  { return "continue" }
func (*ContinueStmt) String() string     { return "continue" }

// BlockStmt 表示一个带标签的基本块。
// 块内最后一条语句必须是终止指令 (br/jump/return)。
type BlockStmt struct {
	Label string // 块标签，如 entry, merge, then
	Body  []Stmt // 块内语句
}

func (*BlockStmt) stmt() {}
func (s *BlockStmt) FirstLine() string { return s.Label }
func (s *BlockStmt) String() string {
	r := s.Label + ": {\n"
	for _, st := range s.Body { r += "\t" + st.String() + "\n" }
	return r + "}"
}

func join(ss []string) string {
	var sb strings.Builder
	for i, v := range ss {
		if i > 0 {
			sb.WriteString(", ")
		}
		if needsQuote(v) {
			sb.WriteByte('\'')
			sb.WriteString(v)
			sb.WriteByte('\'')
		} else {
			sb.WriteString(v)
		}
	}
	return sb.String()
}

func needsQuote(s string) bool {
	if len(s) == 0 {
		return false
	}
	// 裸路径（./xxx 或 /xxx）不加引号。
	if s[0] == '/' || (len(s) >= 2 && s[:2] == "./") {
		return false
	}
	for i := 0; i < len(s); i++ {
		switch s[i] {
		case ' ', '\t', ',', '(', ')', '=', '+', '-', '*', '%', '!', '<', '>', '&', '|', '^', '{', '}', '"', '\'':
			return true
		}
	}
	return false
}
