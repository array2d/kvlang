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
		last := i == len(f.Funcs)-1
		dumpFunc(w, fn, "", last)
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
	fmt.Fprintf(w, "%s%sFunc %q (%s) -> (body %d stmts)\n",
		prefix, branch, fn.Name, fn.Signature[strings.Index(fn.Signature, "("):], len(fn.Body))
	childPrefix := prefix
	if last {
		childPrefix += "    "
	} else {
		childPrefix += "│   "
	}
	for i, st := range fn.Body {
		isLast := i == len(fn.Body)-1
		dumpStmt(w, st, childPrefix, isLast)
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
		fmt.Fprintf(w, "%sInstruction op=%q reads=%v writes=%v\n",
			fullPrefix, s.Opcode, s.Reads, s.Writes)

	case *BlockStmt:
		fmt.Fprintf(w, "%sBlockStmt %q (%d stmts)\n", fullPrefix, s.Label, len(s.Body))
		childPrefix := prefix
		if last {
			childPrefix += "    "
		} else {
			childPrefix += "│   "
		}
		for i, child := range s.Body {
			isLast := i == len(s.Body)-1
			dumpStmt(w, child, childPrefix, isLast)
		}

	case *IfStmt:
		fmt.Fprintf(w, "%sIfStmt cond=%q (then=%d, else=%d)\n",
			fullPrefix, s.Cond, len(s.Then), len(s.Else))
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
		fmt.Fprintf(w, "%sWhileStmt cond=%q (body=%d)\n",
			fullPrefix, s.Cond, len(s.Body))

	case *BreakStmt:
		fmt.Fprintf(w, "%sBreakStmt\n", fullPrefix)

	case *ContinueStmt:
		fmt.Fprintf(w, "%sContinueStmt\n", fullPrefix)

	default:
		fmt.Fprintf(w, "%s%s\n", fullPrefix, s.String())
	}
}
