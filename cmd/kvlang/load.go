package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/keytree"
	"kvlang/internal/kvcpu"
	"kvlang/internal/kvspace"
	"kvlang/internal/logx"
	"kvlang/internal/parser"
	"kvlang/internal/vthread"
)

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

func loadFunctions(kv kvspace.KVSpace, files []string) {
	ctx := context.Background()
	hasMain := false
	var allPreamble []string
	for _, f := range files {
		df, err := parser.ParseFile(f)
		if err != nil { logx.Warn("SKIP %s: %v", f, err); continue }
		for i := range df.Funcs {
			if err := df.Funcs[i].Register(ctx, kv); err != nil { logx.Error("FAIL %s: %v", f, err); continue }
			if df.Funcs[i].Name == "main" { hasMain = true }
		}
		allPreamble = append(allPreamble, df.PreambleLines...)
	}
	if len(allPreamble) == 0 && !hasMain { logx.Fatal("no executable code found") }
	body := make([]string, len(allPreamble)); copy(body, allPreamble)
	if hasMain { body = append(body, "main() -> './pre_main_ret'") }
	preMain := ast.Func{Name: "pre_main", Signature: "def pre_main() -> ()", Body: body}
	preMain.Register(ctx, kv)
	kv.Set(keytree.FuncMain, json.RawMessage(`{"entry":"pre_main","reads":[],"writes":[]}`), 0)
}

// executeEntry 创建 vthread 并执行 (runFile/runcode 共用)。
func executeEntry(kv kvspace.KVSpace) {
	ctx := context.Background()
	st := vthread.VThread{PC: "[0,0]", Status: "init", Mode: "single"}
	data, _ := json.Marshal(st)
	kv.Set(keytree.VThread("run"), data, 0)
	kv.Set(keytree.VThreadSlot("run", 0, 0), "pre_main", 0)
	logx.Info("[single] executing run")
	kvcpu.Execute(ctx, kv, "run")
}

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
