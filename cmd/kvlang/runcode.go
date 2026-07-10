package main

import (
	"encoding/json"
	"io"

	"kvlang/internal/ast"
	"kvlang/internal/layoutcode"
	"kvlang/internal/lower"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/logx"
	"kvlang/internal/parser"
)

func runCode(name string, rc io.Reader) {
	kv := kvspace.Conn("127.0.0.1:6379")
	defer kv.DisConn()
	registerDefaultTerm(kv)

	df, err := parser.ParseCode(rc)
	if err != nil { logx.Fatal("parse: %v", err) }

	hasMain := false
	var allPreamble []string
	for i := range df.Funcs {
		fn := lower.Func(&df.Funcs[i])
		layoutcode.WriteFunc(kv, fn)
		if fn.Name == "main" { hasMain = true }
	}
	allPreamble = df.PreambleLines
	if len(allPreamble) == 0 && !hasMain { logx.Fatal("no executable code found") }

	body := make([]string, len(allPreamble)); copy(body, allPreamble)
	if hasMain { body = append(body, "main() -> './pre_main_ret'") }
	preMain := ast.Func{Name: "pre_main", Signature: "def pre_main() -> ()", Body: toStmts(body)}
	preMain = *lower.Func(&preMain)
	layoutcode.WriteFunc(kv, &preMain)
	kv.Set(keytree.FuncMain, json.RawMessage(`{"entry":"pre_main","reads":[],"writes":[]}`), 0)

	executeEntry(kv)
}
