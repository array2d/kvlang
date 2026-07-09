package main

import (
	"fmt"
	"os"

	"kvlang/internal/parser"
)

func cmdVet(args []string) {
	mode, name, rc := parseInput(args, "vet")
	switch mode {
	case modeServe:
		fmt.Fprintln(os.Stderr, "usage: kvlang vet [<file.kv>]")
		fmt.Fprintln(os.Stderr, "  <file.kv>        validate file")
		fmt.Fprintln(os.Stderr, "  -c \"code\"         validate inline code")
		fmt.Fprintln(os.Stderr, "  echo \"code\" | ...  validate from pipe")
		os.Exit(1)
	case modeFile:
		_, err := parser.ParseFile(name)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
			os.Exit(1)
		}
		fmt.Printf("%s: OK\n", name)
	case modeInline, modePipe:
		_, err := parser.ParseCode(rc)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
			os.Exit(1)
		}
		fmt.Printf("%s: OK\n", name)
	}
}
