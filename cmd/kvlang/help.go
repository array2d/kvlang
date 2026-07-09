package main

import (
	"fmt"
	"os"
)

func showHelp() {
	fmt.Fprintln(os.Stderr, "kvlang — KV language VM interpreter")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "usage: kvlang [flags] [<file.kv>]")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "commands:")
	fmt.Fprintln(os.Stderr, "  help              show this help")
	fmt.Fprintln(os.Stderr, "  vet <file.kv>     validate syntax")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "flags:")
	fmt.Fprintln(os.Stderr, "  -c \"code\"          execute inline code")
	fmt.Fprintln(os.Stderr, "  -h, --help        show this help")
	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "examples:")
	fmt.Fprintln(os.Stderr, "  kvlang                    start daemon")
	fmt.Fprintln(os.Stderr, "  kvlang file.kv            execute file")
	fmt.Fprintln(os.Stderr, "  kvlang vet file.kv        check syntax")
	fmt.Fprintln(os.Stderr, "  kvlang -c '1+2->./x'     inline code")
	fmt.Fprintln(os.Stderr, "  echo 'code' | kvlang      pipe input")
	os.Exit(0)
}
