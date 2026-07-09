// Package ast 定义 kvlang 抽象语法树节点类型。
package ast

import (
	"context"
	"fmt"

	"kvlang/internal/kvspace"
	"kvlang/internal/keytree"
)

// Stmt 表示函数体中的一条语句。
type Stmt interface {
	stmt()
	String() string // 序列化为 kvlang 源码行
}

// Func 表示一个函数定义。
type Func struct {
	Name      string // 函数名
	Signature string // def funcName(A:int, B:int) -> (C:int)
	Body      []Stmt // 函数体语句
}

// Register 将函数定义写入 kvspace 空间。
func (fn *Func) Register(ctx context.Context, kv kvspace.KVSpace) error {
	if err := kv.Set(keytree.SrcFunc(fn.Name), fn.Signature, 0); err != nil {
		return fmt.Errorf("register sig: %w", err)
	}
	for i, st := range fn.Body {
		key := keytree.SrcFuncLine(fn.Name, i)
		if err := kv.Set(key, st.String(), 0); err != nil {
			return fmt.Errorf("register body[%d]: %w", i, err)
		}
	}
	return nil
}

// Instruction 表示一条 kvlang 指令。
type Instruction struct {
	Opcode string   // 操作码
	Reads  []string // 输入参数
	Writes []string // 输出槽位
}

func (i *Instruction) stmt() {}
func (i *Instruction) String() string {
	// Reconstruct kvlang source line
	s := i.Opcode
	if len(i.Reads) > 0 || len(i.Writes) > 0 {
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

// ForStmt 表示 for 循环。
type ForStmt struct {
	Var   string // 迭代变量
	Start string // 起始值
	End   string // 结束值
	Body  []Stmt // 循环体
}

func (*ForStmt) stmt() {}
func (s *ForStmt) String() string {
	r := fmt.Sprintf("for (%s in %s..%s) {\n", s.Var, s.Start, s.End)
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
func (s *WhileStmt) String() string {
	r := "while (" + s.Cond + ") {\n"
	for _, st := range s.Body { r += "\t" + st.String() + "\n" }
	r += "}"
	return r
}

// BreakStmt 表示跳出当前循环。
type BreakStmt struct{}

func (*BreakStmt) stmt() {}
func (*BreakStmt) String() string { return "break" }

// ContinueStmt 表示跳过本次迭代。
type ContinueStmt struct{}

func (*ContinueStmt) stmt() {}
func (*ContinueStmt) String() string { return "continue" }

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
