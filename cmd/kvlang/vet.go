package main

import (
	"fmt"
	"os"

	"kvlang/internal/ast"
	"kvlang/internal/lower"
	"kvlang/internal/parser"
)

func cmdVet(args []string) {
	dump := false
	lowerFlag := false
	filtered := make([]string, 0, len(args))
	for _, a := range args {
		switch a {
		case "--dump":
			dump = true
		case "--lower":
			lowerFlag = true
		default:
			filtered = append(filtered, a)
		}
	}

	mode, name, rc := parseInput(filtered, "vet")
	switch mode {
	case modeServe:
		fmt.Fprintln(os.Stderr, "usage: kvlang vet [--dump] [--lower] [<file.kv>]")
		fmt.Fprintln(os.Stderr, "  <file.kv>        validate file")
		fmt.Fprintln(os.Stderr, "  -c \"code\"         validate inline code")
		fmt.Fprintln(os.Stderr, "  --dump            print AST tree")
		fmt.Fprintln(os.Stderr, "  --lower           lower structured CF → basic blocks")
		fmt.Fprintln(os.Stderr, "  echo \"code\" | ...  validate from pipe")
		os.Exit(1)
	case modeFile:
		df, err := parser.ParseFile(name)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
			os.Exit(1)
		}
		printVetResult(name, df, dump, lowerFlag)
	case modeInline, modePipe:
		df, err := parser.ParseCode(rc)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
			os.Exit(1)
		}
		printVetResult(name, df, dump, lowerFlag)
	}
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
