package main

import (
	"fmt"
	"os"

	"kvlang/internal/ast"
	"kvlang/internal/parser"
)

func cmdFormat(args []string) {
	mode, name, rc := parseInput(args, "format")
	switch mode {
	case modeServe:
		fmt.Fprintln(os.Stderr, "usage: kvlang format [<file.kv>]")
		fmt.Fprintln(os.Stderr, "  <file.kv>        format file (in-place)")
		fmt.Fprintln(os.Stderr, "  -c \"code\"         format inline code")
		fmt.Fprintln(os.Stderr, "  echo \"code\" | ...  format from pipe")
		os.Exit(1)
	case modeFile:
		df, err := parser.ParseFile(name)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
			os.Exit(1)
		}
		ast.Format(os.Stdout, df)
	case modeInline, modePipe:
		df, err := parser.ParseCode(rc)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
			os.Exit(1)
		}
		ast.Format(os.Stdout, df)
	}
}
