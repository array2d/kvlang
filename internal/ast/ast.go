// Package ast 定义 kvlang 抽象语法树节点类型。
package ast

import (
	"fmt"

	"kvlang/internal/kvspace"
	"kvlang/internal/keytree"
)

// Stmt 表示函数体中的一条语句。
type Stmt interface {
	stmt()
	String() string
	FirstLine() string // 返回源码首行，用于 inBody 匹配
	SetKV(kv kvspace.KVSpace, prefix string, idx *int)
}

// Func 表示一个函数定义。
type Func struct {
	Name      string // 函数名
	Signature string // def funcName(A:int, B:int) -> (C:int)
	Body      []Stmt // 函数体语句
}

// Register 将函数定义写入 kvspace 空间。
// Register 将函数签名写入 kvspace（body 由 layoutcode.WriteBody 写入）。
func (fn *Func) Register(kv kvspace.KVSpace) error {
	return kv.Set(keytree.SrcFunc(fn.Name), fn.Signature)
}

// Instruction 表示一条 kvlang 指令。
type Instruction struct {
	Opcode string   // 操作码
	Reads  []string // 输入参数
	Writes []string // 输出槽位
}

// infixOps 中缀符号算子集合。
var infixOps = map[string]bool{
	"+": true, "-": true, "*": true, "/": true, "%": true,
	"==": true, "!=": true, "<": true, ">": true, "<=": true, ">=": true,
	"&&": true, "||": true, "!": true,
	"&": true, "|": true, "^": true, "<<": true, ">>": true,
}

// unaryOps 单目中缀算子（右操作数为空）。
var unaryOps = map[string]bool{"!": true, "-neg": false} // - 既是单目也是双目

func (i *Instruction) stmt() {}
func (i *Instruction) FirstLine() string { return i.String() }
func (i *Instruction) SetKV(kv kvspace.KVSpace, prefix string, idx *int) {
	n := *idx
	kv.Set(fmt.Sprintf("%s/[%d,0]", prefix, n), i.Opcode)
	for j, r := range i.Reads {
		kv.Set(fmt.Sprintf("%s/[%d,-%d]", prefix, n, j+1), r)
	}
	for j, w := range i.Writes {
		kv.Set(fmt.Sprintf("%s/[%d,%d]", prefix, n, j+1), w)
	}
	*idx = n + 1
}
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
func (s *IfStmt) SetKV(kv kvspace.KVSpace, prefix string, idx *int) {}
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
func (s *ForStmt) SetKV(kv kvspace.KVSpace, prefix string, idx *int) {}
func (s *ForStmt) String() string {
	r := fmt.Sprintf("for (%s in %s) {\n", s.Var, s.Iter)
	for _, st := range s.Body { r += "\t" + st.String() + "\n" }
	r += "}"
	return r
}

// WhileStmt 表示 while 循环。
type WhileStmt struct {
	Cond string // 条件表达式
	Body []Stmt // 循环体
}

func (*WhileStmt) stmt() {}
func (*WhileStmt) FirstLine() string { return "while" }
func (s *WhileStmt) SetKV(kv kvspace.KVSpace, prefix string, idx *int) {}
func (s *WhileStmt) String() string {
	r := "while (" + s.Cond + ") {\n"
	for _, st := range s.Body { r += "\t" + st.String() + "\n" }
	r += "}"
	return r
}

// BreakStmt 表示跳出当前循环。
type BreakStmt struct{}

func (*BreakStmt) stmt() {}
func (*BreakStmt) FirstLine() string { return "break" }
func (*BreakStmt) SetKV(kv kvspace.KVSpace, prefix string, idx *int) {}
func (*BreakStmt) String() string { return "break" }

// ContinueStmt 表示跳过本次迭代。
type ContinueStmt struct{}

func (*ContinueStmt) stmt() {}
func (*ContinueStmt) FirstLine() string { return "continue" }
func (*ContinueStmt) SetKV(kv kvspace.KVSpace, prefix string, idx *int) {}
func (*ContinueStmt) String() string { return "continue" }

// BlockStmt 表示一个带标签的基本块。
// 块内最后一条语句必须是终止指令 (br/jump/return)。
type BlockStmt struct {
	Label string // 块标签，如 entry, merge, then
	Body  []Stmt // 块内语句
}

func (*BlockStmt) stmt() {}
func (s *BlockStmt) FirstLine() string { return s.Label }
func (s *BlockStmt) SetKV(kv kvspace.KVSpace, prefix string, idx *int) {
	sub := prefix + "/" + s.Label
	i := 0
	for _, st := range s.Body { st.SetKV(kv, sub, &i) }
}
func (s *BlockStmt) String() string {
	r := s.Label + ": {\n"
	for _, st := range s.Body {
		r += "\t" + st.String() + "\n"
	}
	r += "}"
	return r
}

func join(ss []string) string {
	s := ""
	for i, v := range ss {
		if i > 0 {
			s += ", "
		}
		if needsQuote(v) {
			s += "'" + v + "'"
		} else {
			s += v
		}
	}
	return s
}

func needsQuote(s string) bool {
	if len(s) == 0 {
		return false
	}
	if s[0] == '/' {
		return true
	}
	if len(s) >= 2 && s[:2] == "./" {
		return true
	}
	return false
}

// FormalParams 表示函数签名的形参列表。
type FormalParams struct {
	Reads  []string
	Writes []string
}
