package ast

import (
	"fmt"
	"io"
)

// Format 将 AST 格式化为规范的 kvlang 源码输出到 w。
// 前置行注释（Comments 字段）在对应节点前原样输出（S6：注释保留）。
func Format(w io.Writer, f *File) {
	for fi, fn := range f.Funcs {
		if fi > 0 {
			fmt.Fprintln(w)
		}
		for _, c := range fn.Comments {
			fmt.Fprintln(w, c)
		}
		sig := fn.Sig.String()
		fmt.Fprintf(w, "%s {\n", sig)
		formatBody(w, fn.Body, "\t")
		fmt.Fprintln(w, "}")
	}

	if len(f.TopLevelCalls) > 0 {
		fmt.Fprintln(w)
		for _, inst := range f.TopLevelCalls {
			for _, c := range inst.Comments {
				fmt.Fprintln(w, c)
			}
			fmt.Fprintln(w, inst.String())
		}
	}
}

func formatBody(w io.Writer, stmts []Stmt, indent string) {
	for i, st := range stmts {
		if i > 0 {
			_, prevBlock := stmts[i-1].(*BlockStmt)
			_, curBlock := st.(*BlockStmt)
			_, prevIf := stmts[i-1].(*IfStmt)
			_, curIf := st.(*IfStmt)
			if prevBlock || curBlock || prevIf || curIf {
				fmt.Fprintln(w)
			}
		}

		// 输出前置行注释
		for _, c := range StmtComments(st) {
			fmt.Fprintf(w, "%s%s\n", indent, c)
		}

		switch s := st.(type) {
		case *Instruction:
			fmt.Fprintf(w, "%s%s\n", indent, s.String())

		case *BlockStmt:
			fmt.Fprintf(w, "%s%s: {\n", indent, s.Label)
			formatBody(w, s.Body, indent+"\t")
			fmt.Fprintf(w, "%s}\n", indent)

		case *IfStmt:
			cond := ""
			if s.Cond != nil {
				cond = s.Cond.String()
			}
			fmt.Fprintf(w, "%sif (%s) {\n", indent, cond)
			formatBody(w, s.Then, indent+"\t")
			if len(s.Else) > 0 {
				fmt.Fprintf(w, "%s} else {\n", indent)
				formatBody(w, s.Else, indent+"\t")
			}
			fmt.Fprintf(w, "%s}\n", indent)

		case *ForStmt:
			fmt.Fprintf(w, "%sfor (%s in %s) {\n", indent, s.Var, s.Iter)
			formatBody(w, s.Body, indent+"\t")
			fmt.Fprintf(w, "%s}\n", indent)

		case *WhileStmt:
			cond := ""
			if s.Cond != nil {
				cond = s.Cond.String()
			}
			fmt.Fprintf(w, "%swhile (%s) {\n", indent, cond)
			formatBody(w, s.Body, indent+"\t")
			fmt.Fprintf(w, "%s}\n", indent)

		case *BreakStmt:
			fmt.Fprintf(w, "%sbreak\n", indent)

		case *ContinueStmt:
			fmt.Fprintf(w, "%scontinue\n", indent)
		}
	}
}
