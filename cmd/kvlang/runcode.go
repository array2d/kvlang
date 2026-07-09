package main

import (
	"context"
	"encoding/json"
	"io"

	"kvlang/internal/ast"
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

	ctx := context.Background()
	hasMain := false
	var allPreamble []string
	for i := range df.Funcs {
		if err := df.Funcs[i].Register(ctx, kv); err != nil { logx.Error("FAIL: %v", err); continue }
		if df.Funcs[i].Name == "main" { hasMain = true }
	}
	allPreamble = df.PreambleLines
	if len(allPreamble) == 0 && !hasMain { logx.Fatal("no executable code found") }

	body := make([]string, len(allPreamble)); copy(body, allPreamble)
	if hasMain { body = append(body, "main() -> './pre_main_ret'") }
	preMain := ast.Func{Name: "pre_main", Signature: "def pre_main() -> ()", Body: toStmts(body)}
	preMain.Register(ctx, kv)
	kv.Set(keytree.FuncMain, json.RawMessage(`{"entry":"pre_main","reads":[],"writes":[]}`), 0)

	executeEntry(kv)
}
