package main

import (
	"io"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/internal/layoutrwir"
	"kvlang/internal/logx"
	"kvlang/internal/lower"
	"kvlang/internal/parser"
)

// cmdLayoutRWIRAndRun 先 layoutrwir 再 run（fix-039：替代旧 run 机制）。
func cmdLayoutRWIRAndRun(args []string) {
	if len(args) == 0 {
		runLib("", "init", false)
		return
	}
	cmdLayoutRWIR(args)
	entry := findEntry(defaultKVSpace())
	runLib("", entry, false)
}

func findEntry(dsn string) string {
	kv := kvspace.Conn(dsn)
	defer kv.DisConn()
	if entry := findEntryPrefix(kv, keytree.LibRoot); entry != "" {
		return entry
	}
	return "init"
}

func findEntryPrefix(kv kvspace.KVSpace, prefix string) string {
	children, _ := kv.List(prefix)
	for _, c := range children {
		if strings.HasSuffix(c, ".init") {
			return c
		}
		sub := prefix + "/" + c
		if entry := findEntryPrefix(kv, sub); entry != "" {
			return c + "/" + entry
		}
	}
	return ""
}

// ── 加载 + 执行（组合 layoutrwir.go 和 run.go）────────────────────────

func runFiles(dsn string, paths []string, debug bool) {
	kv := kvspace.Conn(dsn)
	defer kv.DisConn()
	registerDefaultTerm(kv)

	var files []string
	for _, p := range paths {
		f, err := collectKVFiles(p)
		if err != nil { logx.Fatal("collect .kv files: %v", err) }
		files = append(files, f...)
	}
	if len(files) == 0 { logx.Fatal("no .kv files found") }

	if !loadFunctions(kv, files) { return }
	executeEntry(kv, findEntry(dsn), debug)
}

func runCode(name string, rc io.Reader, dsn string, debug bool) {
	kv := kvspace.Conn(dsn)
	defer kv.DisConn()
	registerDefaultTerm(kv)

	df, diags, err := parser.ParseCode(rc)
	if err != nil { logx.Fatal("parse: %v", err) }
	for _, d := range diags { d.SrcName = "<inline>"; logx.Diag(d) }
	if parser.HasErrors(diags) { logx.Fatal("parse: error-level diagnostics — refusing to execute") }
	if len(df.Funcs) == 0 && len(df.TopLevelCalls) == 0 && len(df.InitBody) == 0 { return }
	for i := range df.Funcs {
		fpkg := df.Funcs[i].Pkg
		if fpkg == "" { fpkg = df.Package }
		layoutrwir.WriteFunc(kv, fpkg, lower.Func(&df.Funcs[i]))
	}
	body := df.InitBody
	for _, c := range df.TopLevelCalls { body = append(body, c) }
	if len(body) > 0 {
		initFn := ast.Func{Sig: ast.FuncSig{Name: "init"}, Body: body}
		layoutrwir.WriteFunc(kv, "", lower.Func(&initFn))
	}
	executeEntry(kv, findEntry(dsn), debug)
}
