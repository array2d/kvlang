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
	FirstLine() string
}

// ── 签名 ──────────────────────────────────────────────────────

// Param 表示有名称和可选类型标注的形参。
type Param struct {
	Name string
	Type string // "" = 动态类型
}

// FuncSig 是函数签名的结构化形式。
type FuncSig struct {
	Name    string
	Params  []Param
	Returns []Param
}

// String 重建规范签名字符串：def name(A:int, B:int) -> (C:int)
func (s FuncSig) String() string {
	var sb strings.Builder
	sb.WriteString("def ")
	sb.WriteString(s.Name)
	sb.WriteByte('(')
	for i, p := range s.Params {
		if i > 0 {
			sb.WriteString(", ")
		}
		sb.WriteString(p.Name)
		if p.Type != "" {
			sb.WriteByte(':')
			sb.WriteString(p.Type)
		}
	}
	sb.WriteString(") -> (")
	for i, p := range s.Returns {
		if i > 0 {
			sb.WriteString(", ")
		}
		sb.WriteString(p.Name)
		if p.Type != "" {
			sb.WriteByte(':')
			sb.WriteString(p.Type)
		}
	}
	sb.WriteByte(')')
	return sb.String()
}

// ParamNames 返回输入参数名列表。
func (s FuncSig) ParamNames() []string {
	names := make([]string, len(s.Params))
	for i, p := range s.Params {
		names[i] = p.Name
	}
	return names
}

// ReturnNames 返回输出参数名列表。
func (s FuncSig) ReturnNames() []string {
	names := make([]string, len(s.Returns))
	for i, p := range s.Returns {
		names[i] = p.Name
	}
	return names
}

// ── 函数 ──────────────────────────────────────────────────────

// Func 表示一个函数定义。
type Func struct {
	Sig  FuncSig
	Body []Stmt
}

// FullText 返回函数的完整源码文本，用于存入 /src/<pkg>/<name>。
func (fn *Func) FullText() string {
	var sb strings.Builder
	sb.WriteString(fn.Sig.String())
	sb.WriteString(" {\n")
	for _, st := range fn.Body {
		sb.WriteString("    ")
		sb.WriteString(st.String())
		sb.WriteString("\n")
	}
	sb.WriteString("}")
	return sb.String()
}

// ── Instruction ───────────────────────────────────────────────

// Instruction 表示一条 kvlang 指令。
type Instruction struct {
	Opcode string
	Reads  []string
	Writes []string
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
	if i.Opcode == "br" || i.Opcode == "goto" || i.Opcode == "return" {
		if len(i.Reads) > 0 {
			return i.Opcode + "(" + join(i.Reads) + ")"
		}
		return i.Opcode
	}
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
	s := i.Opcode
	if len(i.Reads) > 0 {
		s += "(" + join(i.Reads) + ")"
	}
	if len(i.Writes) > 0 {
		s += " -> " + join(i.Writes)
	}
	return s
}

// ── 控制流节点 ────────────────────────────────────────────────

// IfStmt 表示 if/else 控制流。
type IfStmt struct {
	Cond *Instruction // 条件表达式，Writes 在 HIR 阶段为 nil
	Then []Stmt
	Else []Stmt
}

func (*IfStmt) stmt() {}
func (*IfStmt) FirstLine() string { return "if" }
func (s *IfStmt) String() string {
	cond := ""
	if s.Cond != nil {
		cond = s.Cond.String()
	}
	r := "if (" + cond + ") {\n"
	for _, st := range s.Then {
		r += "\t" + st.String() + "\n"
	}
	r += "}"
	if len(s.Else) > 0 {
		r += " else {\n"
		for _, st := range s.Else {
			r += "\t" + st.String() + "\n"
		}
		r += "}"
	}
	return r
}

// ForStmt 表示 for 循环：迭代 kvspace 路径上的数据。
type ForStmt struct {
	Var  string
	Iter string
	Body []Stmt
}

func (*ForStmt) stmt() {}
func (*ForStmt) FirstLine() string { return "for" }
func (s *ForStmt) String() string {
	r := fmt.Sprintf("for (%s in %s) {\n", s.Var, s.Iter)
	for _, st := range s.Body {
		r += "\t" + st.String() + "\n"
	}
	return r + "}"
}

// WhileStmt 表示 while 循环。
type WhileStmt struct {
	Cond *Instruction // 条件表达式，Writes 在 HIR 阶段为 nil
	Body []Stmt
}

func (*WhileStmt) stmt() {}
func (*WhileStmt) FirstLine() string { return "while" }
func (s *WhileStmt) String() string {
	cond := ""
	if s.Cond != nil {
		cond = s.Cond.String()
	}
	r := "while (" + cond + ") {\n"
	for _, st := range s.Body {
		r += "\t" + st.String() + "\n"
	}
	return r + "}"
}

// BreakStmt 表示跳出当前循环。
type BreakStmt struct{}

func (*BreakStmt) stmt()             {}
func (*BreakStmt) FirstLine() string { return "break" }
func (*BreakStmt) String() string    { return "break" }

// ContinueStmt 表示跳过本次迭代。
type ContinueStmt struct{}

func (*ContinueStmt) stmt()             {}
func (*ContinueStmt) FirstLine() string { return "continue" }
func (*ContinueStmt) String() string    { return "continue" }

// BlockStmt 表示一个带标签的基本块。
type BlockStmt struct {
	Label string
	Body  []Stmt
}

func (*BlockStmt) stmt() {}
func (s *BlockStmt) FirstLine() string { return s.Label }
func (s *BlockStmt) String() string {
	r := s.Label + ": {\n"
	for _, st := range s.Body {
		r += "\t" + st.String() + "\n"
	}
	return r + "}"
}

// ── 工具函数 ──────────────────────────────────────────────────

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
