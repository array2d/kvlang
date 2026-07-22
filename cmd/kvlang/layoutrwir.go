package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"kvlang/internal/ast"
	"github.com/array2d/kvspace-go"
	"kvlang/internal/layoutrwir"
	"kvlang/internal/logx"
	"kvlang/internal/lower"
	"kvlang/internal/parser"
)

// cmdLayoutRWIR 将 .kv 文件加载进 kvspace，不执行。多文件拼接为单源解析。
func cmdLayoutRWIR(args []string) {
	fs := flag.NewFlagSet("layoutrwir", flag.ExitOnError)
	dsn := fs.String("kvspace", defaultKVSpace(), kvspaceFlagDesc)
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang layoutrwir [--kvspace dsn] <file.kv|dir>...")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	if fs.NArg() == 0 {
		fs.Usage()
		os.Exit(1)
	}

	kv := kvspace.Conn(*dsn)
	defer kv.DisConn()

	var allFiles []string
	for _, arg := range fs.Args() {
		f, err := collectKVFiles(arg)
		if err != nil { logx.Fatal("collect .kv files: %v", err) }
		allFiles = append(allFiles, f...)
	}
	if len(allFiles) == 0 { logx.Fatal("no .kv files found") }

	var src strings.Builder
	for _, f := range allFiles {
		b, err := os.ReadFile(f)
		if err != nil { logx.Fatal("read %s: %v", f, err) }
		src.Write(b)
		src.WriteString("\n")
	}
	df, diags, err := parser.ParseCode(strings.NewReader(src.String()))
	if err != nil { logx.Fatal("parse: %v", err) }
	for _, d := range diags { logx.Diag(d) }
	if parser.HasErrors(diags) { logx.Fatal("parse: error-level diagnostics — refusing to load") }

	anyCode := false
	for i := range df.Funcs {
		fpkg := df.Funcs[i].Pkg
		if fpkg == "" { fpkg = df.Package }
		layoutrwir.WriteFunc(kv, fpkg, lower.Func(&df.Funcs[i]))
		anyCode = true
	}
	body := df.InitBody
	for _, c := range df.TopLevelCalls { body = append(body, c) }
	if len(body) > 0 {
		initFn := ast.Func{Sig: ast.FuncSig{Name: "init"}, Body: body}
		layoutrwir.WriteFunc(kv, "", lower.Func(&initFn))
		anyCode = true
	}
	if !anyCode { logx.Fatal("no executable code found") }
	logx.Info("loaded %d file(s) → ready", len(allFiles))
}

// ── 文件 → kvspace（加载，不涉及执行）─────────────────────────────────

func collectKVFiles(path string) ([]string, error) {
	info, err := os.Stat(path)
	if err != nil { return nil, fmt.Errorf("stat %s: %w", path, err) }
	if !info.IsDir() {
		if strings.HasSuffix(path, ".kv") { return []string{path}, nil }
		return nil, fmt.Errorf("not a .kv file: %s", path)
	}
	var files []string
	filepath.Walk(path, func(p string, info os.FileInfo, err error) error {
		if err != nil { return err }
		if !info.IsDir() && strings.HasSuffix(p, ".kv") { files = append(files, p) }
		return nil
	})
	return files, nil
}

func loadFunctions(kv kvspace.KVSpace, files []string) bool {
	anyCode := false
	loaded := map[string]bool{}
	var initBody []ast.Stmt
	for _, f := range files {
		_loadFile(kv, f, &anyCode, loaded, &initBody)
	}
	if !anyCode { return false }
	if len(initBody) > 0 {
		initFn := ast.Func{Sig: ast.FuncSig{Name: "init"}, Body: initBody}
		layoutrwir.WriteFunc(kv, "", lower.Func(&initFn))
	}
	return true
}

func _loadFile(kv kvspace.KVSpace, f string, anyCode *bool, loaded map[string]bool, initBody *[]ast.Stmt) {
	abs, _ := filepath.Abs(f)
	if loaded[abs] { return }
	loaded[abs] = true

	df, diags, err := parser.ParseFile(f)
	if err != nil { logx.Warn("SKIP %s: %v", f, err); return }
	for _, d := range diags { d.SrcName = f; logx.Diag(d) }
	if parser.HasErrors(diags) { logx.Fatal("%s: error-level diagnostics — refusing to load", f) }

	for i := range df.Funcs {
		fpkg := df.Funcs[i].Pkg
		if fpkg == "" { fpkg = df.Package }
		layoutrwir.WriteFunc(kv, fpkg, lower.Func(&df.Funcs[i]))
		*anyCode = true
	}
	for _, st := range df.InitBody { *initBody = append(*initBody, st) }
	for _, c := range df.TopLevelCalls { *initBody = append(*initBody, c) }
	if len(df.InitBody) > 0 || len(df.TopLevelCalls) > 0 { *anyCode = true }
}

func makeInitFunc(calls []*ast.Instruction) *ast.Func {
	body := make([]ast.Stmt, len(calls))
	for i, inst := range calls {
		body[i] = inst
	}
	initFn := ast.Func{Sig: ast.FuncSig{Name: "init"}, Body: body}
	return lower.Func(&initFn)
}
