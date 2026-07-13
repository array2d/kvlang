package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/layoutcode"
	"kvlang/internal/logx"
	"kvlang/internal/lower"
	"kvlang/internal/parser"
)

// cmdLoad 将 .kv 文件加载进 kvspace，不执行。
func cmdLoad(args []string) {
	fs := flag.NewFlagSet("load", flag.ExitOnError)
	addr := fs.String("addr", "127.0.0.1:6379", "Redis 地址 (host:port)")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang load [--addr host:port] <file.kv|dir>")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	if fs.NArg() == 0 {
		fs.Usage()
		os.Exit(1)
	}

	kv := kvspace.Conn(*addr)
	defer kv.DisConn()

	files, err := collectKVFiles(fs.Arg(0))
	if err != nil { logx.Fatal("collect .kv files: %v", err) }
	if len(files) == 0 { logx.Fatal("no .kv files found in: %s", fs.Arg(0)) }

	loadFunctions(kv, files)
	logx.Info("loaded %d file(s) → ready, run 'kvlang serve' to execute", len(files))
}

// cmdRun 解析参数并路由到对应执行路径：内联 / 文件 / 管道 / serve。
func cmdRun(args []string) {
	fs := flag.NewFlagSet("run", flag.ExitOnError)
	addr := fs.String("addr", "127.0.0.1:6379", "Redis 地址 (host:port)")
	code := fs.String("c", "", "内联代码（直接执行字符串）")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang [--addr host:port] [-c code | <file.kv>]")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	switch {
	case *code != "":
		runCode("inline", strings.NewReader(*code), *addr)
	case fs.NArg() > 0:
		runFile(*addr, fs.Arg(0))
	case !isTerminal():
		runCode("stdin", os.Stdin, *addr)
	default:
		runServe(nil)
	}
}

// runFile 加载 .kv 文件后单次执行。
func runFile(addr, path string) {
	kv := kvspace.Conn(addr)
	defer kv.DisConn()
	registerDefaultTerm(kv)

	files, err := collectKVFiles(path)
	if err != nil { logx.Fatal("collect .kv files: %v", err) }
	if len(files) == 0 { logx.Fatal("no .kv files found in: %s", path) }

	loadFunctions(kv, files)
	executeEntry(kv)
}

// runCode 从 io.Reader 加载代码后单次执行（内联 / 管道模式）。
func runCode(name string, rc io.Reader, addr string) {
	kv := kvspace.Conn(addr)
	defer kv.DisConn()
	registerDefaultTerm(kv)

	df, diags, err := parser.ParseCode(rc)
	if err != nil { logx.Fatal("parse: %v", err) }
	for _, d := range diags { logx.Warn("parse: %s", d) }

	hasMain := false
	for i := range df.Funcs {
		fn := lower.Func(&df.Funcs[i])
		layoutcode.WriteFunc(kv, "main", fn)
		if fn.Sig.Name == "main" { hasMain = true }
	}
	calls := df.TopLevelCalls
	if len(calls) == 0 && !hasMain { logx.Fatal("no executable code found") }

	body := callsToStmts(calls)
	if hasMain { body = append(body, &ast.Instruction{Opcode: "main", Writes: []string{"./pre_main_ret"}}) }
	preMain := ast.Func{Sig: ast.FuncSig{Name: "pre_main"}, Body: body}
	preMain = *lower.Func(&preMain)
	layoutcode.WriteFunc(kv, "main", &preMain)
	kv.Set(keytree.FuncMain, `{"entry":"pre_main","reads":[],"writes":[]}`)

	executeEntry(kv)
}

// loadFunctions 将多个 .kv 文件解析、lower 并写入 kvspace，合成 pre_main 入口。
func loadFunctions(kv kvspace.KVSpace, files []string) {
	hasMain := false
	var allCalls []*ast.Instruction
	for _, f := range files {
		df, diags, err := parser.ParseFile(f)
		if err != nil { logx.Warn("SKIP %s: %v", f, err); continue }
		for _, d := range diags { logx.Warn("%s: %s", f, d) }
		pkg := packageFromPath(f)
		for i := range df.Funcs {
			fn := lower.Func(&df.Funcs[i])
			layoutcode.WriteFunc(kv, pkg, fn)
			if fn.Sig.Name == "main" { hasMain = true }
		}
		allCalls = append(allCalls, df.TopLevelCalls...)
	}
	if len(allCalls) == 0 && !hasMain { logx.Fatal("no executable code found") }
	body := callsToStmts(allCalls)
	if hasMain { body = append(body, &ast.Instruction{Opcode: "main", Writes: []string{"./pre_main_ret"}}) }
	preMain := ast.Func{Sig: ast.FuncSig{Name: "pre_main"}, Body: body}
	preMain = *lower.Func(&preMain)
	layoutcode.WriteFunc(kv, "main", &preMain)
	kv.Set(keytree.FuncMain, `{"entry":"pre_main","reads":[],"writes":[]}`)
}

// packageFromPath 从 .kv 文件路径中推导包名。
// 取文件所在目录的末级名称，当目录为 "." 或空时返回 "main"。
func packageFromPath(path string) string {
	dir := filepath.Dir(path)
	base := filepath.Base(dir)
	if base == "." || base == "" || base == "/" {
		return "main"
	}
	return base
}

// collectKVFiles 收集 path（文件或目录）下所有 .kv 文件路径。
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

// callsToStmts 将顶层调用列表转换为语句列表。
func callsToStmts(calls []*ast.Instruction) []ast.Stmt {
	stmts := make([]ast.Stmt, len(calls))
	for i, inst := range calls {
		stmts[i] = inst
	}
	return stmts
}
