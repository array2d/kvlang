package main

import (
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
// 加载后可由 serve daemon 或后续 run 命令执行。
func cmdLoad(args []string) {
	addr := "127.0.0.1:6379"
	var rest []string
	for i := 0; i < len(args); i++ {
		if args[i] == "--addr" && i+1 < len(args) && strings.Contains(args[i+1], ":") {
			addr = args[i+1]
			i++
		} else {
			rest = append(rest, args[i])
		}
	}
	args = rest
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, "usage: kvlang load [--addr host:port] <file.kv|dir>")
		os.Exit(1)
	}

	kv := kvspace.Conn(addr)
	defer kv.DisConn()

	files, err := collectKVFiles(args[0])
	if err != nil { logx.Fatal("collect .kv files: %v", err) }
	if len(files) == 0 { logx.Fatal("no .kv files found in: %s", args[0]) }

	loadFunctions(kv, files)
	logx.Info("loaded %d file(s) → ready, run 'kvlang serve' to execute", len(files))
}

// cmdRun 根据输入模式路由到对应执行路径。
func cmdRun(args []string) {
	mode, name, rc := parseInput(args, "run")
	switch mode {
	case modeServe:
		runServe()
	case modeFile:
		runFile(args)
	case modeInline, modePipe:
		runCode(name, rc)
	}
}

// runFile 加载 .kv 文件后单次执行。
func runFile(args []string) {
	addr := "127.0.0.1:6379"
	if len(args) > 1 { addr = args[1] }
	path := args[0]

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
	kv.Set(keytree.FuncMain, `{"entry":"pre_main","reads":[],"writes":[]}`, 0)

	executeEntry(kv)
}

// loadFunctions 将多个 .kv 文件解析、lower 并写入 kvspace，合成 pre_main 入口。
func loadFunctions(kv kvspace.KVSpace, files []string) {
	hasMain := false
	var allPreamble []string
	for _, f := range files {
		df, err := parser.ParseFile(f)
		if err != nil { logx.Warn("SKIP %s: %v", f, err); continue }
		for i := range df.Funcs {
			fn := lower.Func(&df.Funcs[i])
			layoutcode.WriteFunc(kv, fn)
			if fn.Name == "main" { hasMain = true }
		}
		allPreamble = append(allPreamble, df.PreambleLines...)
	}
	if len(allPreamble) == 0 && !hasMain { logx.Fatal("no executable code found") }
	body := make([]string, len(allPreamble)); copy(body, allPreamble)
	if hasMain { body = append(body, "main() -> './pre_main_ret'") }
	preMain := ast.Func{Name: "pre_main", Signature: "def pre_main() -> ()", Body: toStmts(body)}
	preMain = *lower.Func(&preMain)
	layoutcode.WriteFunc(kv, &preMain)
	kv.Set(keytree.FuncMain, `{"entry":"pre_main","reads":[],"writes":[]}`, 0)
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

// toStmts 将 kvlang 源码行转换为 AST 语句列表。
func toStmts(lines []string) []ast.Stmt {
	var stmts []ast.Stmt
	for _, line := range lines {
		inst, err := parser.ParseLine(line)
		if err == nil && inst != nil {
			stmts = append(stmts, inst)
		}
	}
	return stmts
}

