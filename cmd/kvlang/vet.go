package main

import (
	"flag"
	"fmt"
	"os"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/lower"
	"kvlang/internal/parser"
)

func cmdVet(args []string) {
	fs := flag.NewFlagSet("vet", flag.ExitOnError)
	dump      := fs.Bool("dump",  false, "输出 AST 树形结构")
	lowerFlag := fs.Bool("lower", false, "lower 结构化控制流 → 基本块")
	code      := fs.String("c",   "",    "内联代码")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang vet [--dump] [--lower] [-c code | <file.kv>]")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	var (name string; rc interface{ Read([]byte) (int, error) })
	switch {
	case *code != "":
		name = "inline"
		rc = strings.NewReader(*code)
	case fs.NArg() > 0:
		name = fs.Arg(0)
	case !isTerminal():
		name, rc = "stdin", os.Stdin
	default:
		fs.Usage()
		os.Exit(1)
	}

	var (df *ast.File; err error; diags []parser.Diagnostic)
	if name == "inline" || name == "stdin" {
		df, diags, err = parser.ParseCode(rc)
	} else {
		df, diags, err = parser.ParseFile(name)
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
		os.Exit(1)
	}
	for _, d := range diags {
		loc := fmt.Sprintf("%s:%d:%d: %s", name, d.Pos.Line, d.Pos.Col, d.Message)
		if d.Source != "" {
			fmt.Fprintf(os.Stderr, "%s\n  %s\n  %s%c\n", loc, d.Source,
				strings.Repeat(" ", d.Pos.Col-1), '^')
		} else {
			fmt.Fprintln(os.Stderr, loc)
		}
	}
	if parser.HasErrors(diags) {
		fmt.Fprintf(os.Stderr, "%s: FAIL\n", name)
		os.Exit(1)
	}
	printVetResult(name, df, *dump, *lowerFlag)
}

func printVetResult(name string, df *ast.File, dump, lowerFlag bool) {
	if lowerFlag {
		df = lower.File(df)
	}
	if dump {
		ast.Dump(os.Stdout, df)
	} else {
		fmt.Printf("%s: OK\n", name)
	}
}
