package main

import (
	"flag"
	"fmt"
	"os"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/parser"
)

func cmdFormat(args []string) {
	fs := flag.NewFlagSet("format", flag.ExitOnError)
	code := fs.String("c", "", "内联代码")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang format [-c code | <file.kv>]")
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

	var df *ast.File
	var err error
	if name == "inline" || name == "stdin" {
		df, err = parser.ParseCode(rc)
	} else {
		df, err = parser.ParseFile(name)
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
		os.Exit(1)
	}
	ast.Format(os.Stdout, df)
}
