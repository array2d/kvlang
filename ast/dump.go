package ast

import (
	"fmt"
	"io"
	"strings"
)

// Dump 以树形结构将 AST 打印到 w（对标 Clang -ast-dump）。
func Dump(w io.Writer, f *File) {
	fmt.Fprintf(w, "File (%d funcs, %d top-level calls)\n", len(f.Funcs), len(f.TopLevelCalls))
	for i, fn := range f.Funcs {
		dumpFunc(w, fn, "", i == len(f.Funcs)-1)
	}
	if len(f.TopLevelCalls) > 0 {
		fmt.Fprintln(w, "TopLevelCalls:")
		for _, inst := range f.TopLevelCalls {
			fmt.Fprintf(w, "  %s\n", inst.String())
		}
	}
}

func dumpFunc(w io.Writer, fn Func, prefix string, last bool) {
	branch := "├── "
	if last {
		branch = "└── "
	}
	sigStr := fn.Sig.String()
	paramsDisplay := sigStr
	if idx := strings.Index(sigStr, "("); idx >= 0 {
		paramsDisplay = sigStr[idx:]
	}
	fmt.Fprintf(w, "%s%sFunc %q %s (body %d stmts)\n",
		prefix, branch, fn.Sig.Name, paramsDisplay, len(fn.Body))
	childPrefix := prefix
	if last {
		childPrefix += "    "
	} else {
		childPrefix += "│   "
	}
	for i, st := range fn.Body {
		dumpStmt(w, st, childPrefix, i == len(fn.Body)-1)
	}
}

func dumpStmt(w io.Writer, st Stmt, prefix string, last bool) {
	branch := "├── "
	if last {
		branch = "└── "
	}
	fullPrefix := prefix + branch

	switch s := st.(type) {
	case *Instruction:
		exprStr := s.Expr.String()
		fmt.Fprintf(w, "%sInstruction expr=%q writes=%v\n",
			fullPrefix, exprStr, s.Writes)

	case *BlockStmt:
		fmt.Fprintf(w, "%sBlockStmt %q (%d stmts)\n", fullPrefix, s.Label, len(s.Body))
		childPrefix := prefix
		if last {
			childPrefix += "    "
		} else {
			childPrefix += "│   "
		}
		for i, child := range s.Body {
			dumpStmt(w, child, childPrefix, i == len(s.Body)-1)
		}

	case *IfStmt:
		condStr := ""
		if s.Cond != nil {
			condStr = s.Cond.String()
		}
		fmt.Fprintf(w, "%sIfStmt cond=%q (then=%d, else=%d)\n",
			fullPrefix, condStr, len(s.Then), len(s.Else))
		childPrefix := prefix
		if last {
			childPrefix += "    "
		} else {
			childPrefix += "│   "
		}
		for i, child := range s.Then {
			dumpStmt(w, child, childPrefix+"  ", i == len(s.Then)-1 && len(s.Else) == 0)
		}
		for i, child := range s.Else {
			dumpStmt(w, child, childPrefix+"  ", i == len(s.Else)-1)
		}

	case *ForStmt:
		fmt.Fprintf(w, "%sForStmt var=%q iter=%q (body=%d)\n",
			fullPrefix, s.Var, s.Iter, len(s.Body))

	case *WhileStmt:
		condStr := ""
		if s.Cond != nil {
			condStr = s.Cond.String()
		}
		fmt.Fprintf(w, "%sWhileStmt cond=%q (body=%d)\n",
			fullPrefix, condStr, len(s.Body))

	case *BreakStmt:
		fmt.Fprintf(w, "%sBreakStmt\n", fullPrefix)

	case *ContinueStmt:
		fmt.Fprintf(w, "%sContinueStmt\n", fullPrefix)

	default:
		fmt.Fprintf(w, "%s%s\n", fullPrefix, s.String())
	}
}
