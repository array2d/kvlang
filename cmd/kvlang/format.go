package main

import (
	"flag"
	"fmt"
	"os"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/logx"
	"kvlang/internal/parser"
)

func cmdFormat(args []string) {
	fs := flag.NewFlagSet("format", flag.ExitOnError)
	write := fs.Bool("w", false, "原地写入文件（对标 gofmt -w）")
	code  := fs.String("c", "", "内联代码")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang format [-w] [-c code | <file.kv>]")
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
		logx.Error("%s: %v", name, err)
		os.Exit(1)
	}
	for _, d := range diags {
		d.SrcName = name; logx.Diag(d)
	}

	if *write && name != "inline" && name != "stdin" {
		f, err := os.Create(name)
		if err != nil { logx.Error("%s: %v", name, err); os.Exit(1) }
		defer f.Close()
		ast.Format(f, df)
	} else {
		ast.Format(os.Stdout, df)
	}
}
