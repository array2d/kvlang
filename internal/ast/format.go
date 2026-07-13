package ast

import (
	"fmt"
	"io"
	"strings"
)

// Format 将 AST 格式化为规范的 kvlang 源码输出到 w。
func Format(w io.Writer, f *File) {
	for fi, fn := range f.Funcs {
		if fi > 0 {
			fmt.Fprintln(w)
		}
		sig := fn.Signature
	sig, _ = strings.CutPrefix(sig, "def ")
	fmt.Fprintf(w, "def %s {\n", sig)
		formatBody(w, fn.Body, "\t")
		fmt.Fprintln(w, "}")
	}

	if len(f.TopLevelCalls) > 0 {
		fmt.Fprintln(w)
		for _, inst := range f.TopLevelCalls {
			fmt.Fprintln(w, inst.String())
		}
	}
}

func formatBody(w io.Writer, stmts []Stmt, indent string) {
	for i, st := range stmts {
		// 空行分隔 block 和控制流结构
		if i > 0 {
			_, prevBlock := stmts[i-1].(*BlockStmt)
			_, curBlock := st.(*BlockStmt)
			_, prevIf := stmts[i-1].(*IfStmt)
			_, curIf := st.(*IfStmt)
			if prevBlock || curBlock || prevIf || curIf {
				fmt.Fprintln(w)
			}
		}

		switch s := st.(type) {
		case *Instruction:
			fmt.Fprintf(w, "%s%s\n", indent, s.String())

		case *BlockStmt:
			fmt.Fprintf(w, "%s%s: {\n", indent, s.Label)
			formatBody(w, s.Body, indent+"\t")
			fmt.Fprintf(w, "%s}\n", indent)

		case *IfStmt:
			fmt.Fprintf(w, "%sif (%s) {\n", indent, s.Cond)
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
			fmt.Fprintf(w, "%swhile (%s) {\n", indent, s.Cond)
			formatBody(w, s.Body, indent+"\t")
			fmt.Fprintf(w, "%s}\n", indent)

		case *BreakStmt:
			fmt.Fprintf(w, "%sbreak\n", indent)

		case *ContinueStmt:
			fmt.Fprintf(w, "%scontinue\n", indent)
		}
	}
}
