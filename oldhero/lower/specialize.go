package lower

import (
	"oldhero/ast"
	"oldhero/rwir/builtin"
	"oldhero/symbol"
)

// Specialize 将多态数值 op（add/+、eq/== 等）按读参类型特化为 /lib/{kind}/{op}。
// 选项 A：同 kind 保 kind，混型取更宽（int 按位宽、float 比 int 宽）。
func Specialize(fn *ast.Func, tm map[string]string) {
	specializeBody(fn.Body, tm)
}

func specializeBody(body []ast.Stmt, tm map[string]string) {
	for _, st := range body {
		switch s := st.(type) {
		case *ast.Instruction:
			specializeInst(s, tm)
		case *ast.ScopeStmt:
			specializeBody(s.Body, tm)
		case *ast.IfStmt:
			if s.Cond != nil {
				specializeInst(s.Cond, tm)
			}
			specializeBody(s.Then, tm)
			specializeBody(s.Else, tm)
		case *ast.WhileStmt:
			if s.Cond != nil {
				specializeInst(s.Cond, tm)
			}
			specializeBody(s.Body, tm)
		case *ast.ForStmt:
			specializeBody(s.Body, tm)
		}
	}
}

func specializeInst(inst *ast.Instruction, tm map[string]string) {
	if inst.Expr == nil || inst.Expr.IsLeaf() {
		return
	}
	opcode := inst.Expr.Op
	word := symbol.Lookup(opcode).Word
	if word == "" {
		word = opcode
	}
	if !builtin.NumOp(word) {
		return
	}
	kind := ""
	for _, arg := range inst.Expr.Args {
		t := slotType(arg.Val, tm)
		if !builtin.IsNumKind(t) {
			continue
		}
		if kind == "" {
			kind = t
		} else {
			kind = builtin.WiderNumKind(kind, t)
		}
	}
	if kind == "" {
		return
	}
	kind = builtin.OpKind(word, kind)
	if kind == "" {
		return
	}
	inst.Expr.Op = kind + "." + word
}
