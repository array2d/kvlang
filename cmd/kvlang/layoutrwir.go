package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/keytree"
	"github.com/array2d/kvlang-go"
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
	// 参数是文件路径 → layoutrwir + run
	cmdLayoutRWIR(args)
	runLib("", "init", false)
}

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

	// 收集全部文件，拼接为单源（fix-039）
	var allFiles []string
	for _, arg := range fs.Args() {
		f, err := collectKVFiles(arg)
		if err != nil { logx.Fatal("collect .kv files: %v", err) }
		allFiles = append(allFiles, f...)
	}
	if len(allFiles) == 0 { logx.Fatal("no .kv files found") }

	// 多文件拼接→单一 source→parse→load（fix-039）
	var src strings.Builder
	srcMap := make(map[int]string) // line→file 映射
	line := 1
	for _, f := range allFiles {
		b, err := os.ReadFile(f)
		if err != nil { logx.Fatal("read %s: %v", f, err) }
		for i := 0; i < strings.Count(string(b),"\n")+1; i++ {
			srcMap[line+i] = f
		}
		line += strings.Count(string(b), "\n") + 1
		src.Write(b)
		src.WriteString("\n")
	}
	df, diags, err := parser.ParseCode(strings.NewReader(src.String()))
	if err != nil { logx.Fatal("parse: %v", err) }
	for _, d := range diags { logx.Warn("parse: %s", d) }
	if parser.HasErrors(diags) { logx.Fatal("parse: error-level diagnostics — refusing to load") }

	// 注册全部函数
	anyCode := false
	for i := range df.Funcs {
		fpkg := df.Funcs[i].Pkg
		if fpkg == "" { fpkg = df.Package }
		layoutrwir.WriteFunc(kv, fpkg, lower.Func(&df.Funcs[i]))
		anyCode = true
	}
	// 写入口 init
	body := df.InitBody
	for _, c := range df.TopLevelCalls { body = append(body, c) }
	if len(body) > 0 {
		initFn := ast.Func{Sig: ast.FuncSig{Name: "init"}, Body: body}
		layoutrwir.WriteFunc(kv, "", lower.Func(&initFn)) // init 永远匿名 lib（空 pkg）
		anyCode = true
	}
	if !anyCode { logx.Fatal("no executable code found") }
	kv.Set(keytree.LibMain, kvspace.Str(`{"entry":"init","reads":[],"writes":[]}`))
	// 源码映射存入 kvspace 供错误定位
	if len(srcMap) > 0 {
		b, _ := json.Marshal(srcMap)
		kv.Set(keytree.LibSrcMap(), kvspace.Bytes(b))
	}
	logx.Info("loaded %d file(s) → ready", len(allFiles))
}

// cmdRun 解析参数并路由：内联 / {lib}.{func} / 文件 / 管道 / serve。
func cmdRun(args []string) {
	fs := flag.NewFlagSet("run", flag.ExitOnError)
	dsn   := fs.String("kvspace", defaultKVSpace(), kvspaceFlagDesc)
	code  := fs.String("c", "", "内联代码（直接执行字符串）")
	debug := fs.Bool("debug", false, "单步调试模式（交互式，每条指令暂停）")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang run [--debug] [-c code | {lib}.{func} | <file.kv|dir>]")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	switch {
	case *code != "":
		runCode("inline", strings.NewReader(*code), *dsn, *debug)
	case fs.NArg() > 0:
		arg := fs.Arg(0)
		// {lib}.{func} 格式（无 .kv 后缀，含 .）→ 执行 lib 函数
		if !strings.HasSuffix(arg, ".kv") && strings.Contains(arg, ".") {
			parts := strings.SplitN(arg, ".", 2)
			runLib(parts[0], parts[1], *debug)
		} else if !strings.HasSuffix(arg, ".kv") {
			// 裸名 → {name}.init
			runLib(arg, "init", *debug)
		} else {
			runFiles(*dsn, fs.Args(), *debug)
		}
	case !isTerminal():
		runCode("stdin", os.Stdin, *dsn, *debug)
	default:
		// 无参数 → .init
		runLib("", "init", false)
	}
}

// runFile 加载 .kv 文件后单次执行。
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

	if !loadFunctions(kv, files) { return } // 纯 def/lib 无 init → 等同 load，不执行
	executeEntry(kv, debug)
}

// runCode 从 io.Reader 加载代码后单次执行（内联 / 管道模式）。
func runCode(name string, rc io.Reader, dsn string, debug bool) {
	kv := kvspace.Conn(dsn)
	defer kv.DisConn()
	registerDefaultTerm(kv)

	df, diags, err := parser.ParseCode(rc)
	if err != nil { logx.Fatal("parse: %v", err) }
	for _, d := range diags { d.SrcName = "<inline>"; logx.Warn("parse: %s", d) }
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
		layoutrwir.WriteFunc(kv, "", lower.Func(&initFn)) // init 永远匿名 lib（空 pkg）
	}
	kv.Set(keytree.LibMain, kvspace.Str(`{"entry":"init","reads":[],"writes":[]}`))
	executeEntry(kv, debug)
}

// loadFunctions 将多个 .kv 文件解析、lower 并写入 kvspace，合成 init 入口。
// import 在 kvspace 模型中为文档级声明——多文件 run 时全部函数已自然就绪（fix-033）。
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
		layoutrwir.WriteFunc(kv, "main", lower.Func(&initFn))
	}
	kv.Set(keytree.LibMain, kvspace.Str(`{"entry":"init","reads":[],"writes":[]}`))
	return true
}

func _loadFile(kv kvspace.KVSpace, f string, anyCode *bool, loaded map[string]bool, initBody *[]ast.Stmt) {
	abs, _ := filepath.Abs(f)
	if loaded[abs] { return }
	loaded[abs] = true

	df, diags, err := parser.ParseFile(f)
	if err != nil { logx.Warn("SKIP %s: %v", f, err); return }
	for _, d := range diags { d.SrcName = f; logx.Warn("%s: %s", f, d) }
	if parser.HasErrors(diags) { logx.Fatal("%s: error-level diagnostics — refusing to load", f) }

	for i := range df.Funcs {
		fpkg := df.Funcs[i].Pkg
		if fpkg == "" { fpkg = df.Package }
		layoutrwir.WriteFunc(kv, fpkg, lower.Func(&df.Funcs[i]))
		*anyCode = true
	}
	// InitBody + TopLevelCalls 追加到收集器（fix-038：多文件按序串联）
	for _, st := range df.InitBody {
		*initBody = append(*initBody, st)
	}
	for _, c := range df.TopLevelCalls {
		*initBody = append(*initBody, c)
	}
	if len(df.InitBody) > 0 || len(df.TopLevelCalls) > 0 {
		*anyCode = true
	}
}

// runLib 执行 /lib/{lib}.{func}（fix-039）。lib/func 为空时默认 ".init"。
func runLib(lib, fn string, debug bool) {
	if fn == "" { fn = "init" }
	name := lib + "." + fn
	if lib == "" { name = fn }
	kv := kvspace.Conn(defaultKVSpace())
	defer kv.DisConn()
	registerDefaultTerm(kv)
	kv.Set(keytree.LibMain, kvspace.Str(fmt.Sprintf(`{"entry":"%s","reads":[],"writes":[]}`, name)))
	executeEntry(kv, debug)
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
