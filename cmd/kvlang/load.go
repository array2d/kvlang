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
	"github.com/array2d/kvlang-go"
	"kvlang/internal/layoutcode"
	"kvlang/internal/logx"
	"kvlang/internal/lower"
	"kvlang/internal/parser"
)

// cmdLoad 将 .kv 文件加载进 kvspace，不执行。
func cmdLoad(args []string) {
	fs := flag.NewFlagSet("load", flag.ExitOnError)
	dsn := fs.String("kvspace", defaultKVSpace(), kvspaceFlagDesc)
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang load [--kvspace dsn] <file.kv|dir>")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	if fs.NArg() == 0 {
		fs.Usage()
		os.Exit(1)
	}

	kv := kvspace.Conn(*dsn)
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
	dsn   := fs.String("kvspace", defaultKVSpace(), kvspaceFlagDesc)
	code  := fs.String("c", "", "内联代码（直接执行字符串）")
	debug := fs.Bool("debug", false, "单步调试模式（交互式，每条指令暂停）")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang [--kvspace dsn] [--debug] [-c code | <file.kv|dir>]")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	switch {
	case *code != "":
		runCode("inline", strings.NewReader(*code), *dsn, *debug)
	case fs.NArg() > 0:
		runFile(*dsn, fs.Arg(0), *debug)
	case !isTerminal():
		runCode("stdin", os.Stdin, *dsn, *debug)
	default:
		runServe(nil)
	}
}

// runFile 加载 .kv 文件后单次执行。
func runFile(dsn, path string, debug bool) {
	kv := kvspace.Conn(dsn)
	defer kv.DisConn()
	registerDefaultTerm(kv)

	files, err := collectKVFiles(path)
	if err != nil { logx.Fatal("collect .kv files: %v", err) }
	if len(files) == 0 { logx.Fatal("no .kv files found in: %s", path) }

	loadFunctions(kv, files)
	executeEntry(kv, debug)
}

// runCode 从 io.Reader 加载代码后单次执行（内联 / 管道模式）。
func runCode(name string, rc io.Reader, dsn string, debug bool) {
	kv := kvspace.Conn(dsn)
	defer kv.DisConn()
	registerDefaultTerm(kv)

	df, diags, err := parser.ParseCode(rc)
	if err != nil { logx.Fatal("parse: %v", err) }
	for _, d := range diags { logx.Warn("parse: %s", d) }
	if len(df.Funcs) == 0 && len(df.TopLevelCalls) == 0 { logx.Fatal("no executable code found") }

	for i := range df.Funcs {
		layoutcode.WriteFunc(kv, "main", lower.Func(&df.Funcs[i]))
	}
	layoutcode.WriteFunc(kv, "main", makeInitFunc(df.TopLevelCalls))
	kv.Set(keytree.FuncMain, kvspace.Str(`{"entry":"init","reads":[],"writes":[]}`))
	executeEntry(kv, debug)
}

// loadFunctions 将多个 .kv 文件解析、lower 并写入 kvspace，合成 init 入口。
func loadFunctions(kv kvspace.KVSpace, files []string) {
	var allCalls []*ast.Instruction
	anyCode := false
	for _, f := range files {
		df, diags, err := parser.ParseFile(f)
		if err != nil { logx.Warn("SKIP %s: %v", f, err); continue }
		for _, d := range diags { logx.Warn("%s: %s", f, d) }
		pkg := packageFromPath(f)
		for i := range df.Funcs {
			layoutcode.WriteFunc(kv, pkg, lower.Func(&df.Funcs[i]))
			anyCode = true
		}
		allCalls = append(allCalls, df.TopLevelCalls...)
		if len(df.TopLevelCalls) > 0 { anyCode = true }
	}
	if !anyCode { logx.Fatal("no executable code found") }
	layoutcode.WriteFunc(kv, "main", makeInitFunc(allCalls))
	kv.Set(keytree.FuncMain, kvspace.Str(`{"entry":"init","reads":[],"writes":[]}`))
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

// makeInitFunc 将所有顶层暴露指令封装为 init() 函数。
//
// kvlang 执行模型：
//   - .kv 文件中所有顶层（函数定义外）的调用指令构成 init() 体
//   - Bootstrap 始终以 init 为唯一入口启动 vthread
//   - main() 是普通函数，无特殊地位；需执行时在顶层直接调用即可
func makeInitFunc(calls []*ast.Instruction) *ast.Func {
	body := make([]ast.Stmt, len(calls))
	for i, inst := range calls {
		body[i] = inst
	}
	initFn := ast.Func{Sig: ast.FuncSig{Name: "init"}, Body: body}
	return lower.Func(&initFn)
}
