package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"kvlang/internal/ast"
	"kvlang/internal/logx"
	"kvlang/internal/parser"
	"kvlang/internal/vm"
	"kvlang/internal/vthread"
	"kvlang/internal/keytree"

	"github.com/redis/go-redis/v9"
)

func cmdRun(args []string) {
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, "usage: kvlang run <path|vtid> [redis_addr]")
		os.Exit(1)
	}
	target := args[0]
	addr := "127.0.0.1:6379"
	if len(args) > 1 {
		addr = args[1]
	}

	ctx := context.Background()
	rdb := redis.NewClient(&redis.Options{Addr: addr})
	defer rdb.Close()

	if err := rdb.Ping(ctx).Err(); err != nil {
		logx.Error("redis connect failed: %v", err)
		os.Exit(1)
	}

	// vtid (纯数字) → 直接执行
	if isNumeric(target) {
		vs := vthread.Get(ctx, rdb, target)
		if vs.Status != "init" {
			logx.Warn("vthread %s status=%s (expect init)", target, vs.Status)
			os.Exit(1)
		}
		logx.Info("[single] executing vthread %s", target)
		vm.Execute(ctx, rdb, target)
		time.Sleep(3 * time.Second)
		vs = vthread.Get(ctx, rdb, target)
		fmt.Printf("\n=== VThread %s ===\n", target)
		fmt.Printf("  PC:     %s\n", vs.PC)
		fmt.Printf("  Status: %s\n", vs.Status)
		if vs.Error != nil {
			fmt.Printf("  Error:  %v\n", vs.Error)
		}
		return
	}

	// 文件路径 → 加载 → 执行
	loadAndRun(ctx, rdb, target)
}

func isNumeric(s string) bool {
	_, err := strconv.Atoi(s)
	return err == nil
}

func loadAndRun(ctx context.Context, rdb *redis.Client, path string) {
	files, err := collectKVFiles(path)
	if err != nil {
		logx.Fatal("collect .kv files: %v", err)
	}
	if len(files) == 0 {
		logx.Fatal("no .kv files found in: %s", path)
	}

	logx.Info("found %d .kv file(s)", len(files))
	loaded := 0
	hasMain := false
	var allPreamble []string

	for _, f := range files {
		df, err := parser.ParseFile(f)
		if err != nil {
			logx.Warn("SKIP %s: %v", f, err)
			continue
		}
		for i := range df.Funcs {
			fn := &df.Funcs[i]
			if err := fn.Register(ctx, rdb); err != nil {
				logx.Error("FAIL %s: %v", f, err)
				continue
			}
			loaded++
			logx.Info("OK   %s → /src/func/%s", f, fn.Name)
			if fn.Name == "main" {
				hasMain = true
			}
		}
		allPreamble = append(allPreamble, df.PreambleLines...)
	}

	if len(allPreamble) == 0 {
		logx.Fatal("no executable code found")
	}

	body := make([]string, len(allPreamble))
	copy(body, allPreamble)
	if hasMain {
		body = append(body, "main() -> './pre_main_ret'")
	}

	preMain := ast.Func{
		Name:      "pre_main",
		Signature: "def pre_main() -> ()",
		Body:      body,
	}
	if err := preMain.Register(ctx, rdb); err != nil {
		logx.Fatal("FAIL register pre_main: %v", err)
	}

	entry, _ := json.Marshal(map[string]any{
		"entry":  "pre_main",
		"reads":  []string{},
		"writes": []string{},
	})
	rdb.Set(ctx, keytree.FuncMain, entry, 0)

	// 创建 vthread 并执行
	vtid := fmt.Sprintf("test-%d", time.Now().UnixNano())
	st := vthread.VThread{PC: "[0,0]", Status: "init", Mode: "single"}
	data, _ := json.Marshal(st)
	rdb.Set(ctx, keytree.VThread(vtid), data, 0)

	pipe := rdb.Pipeline()
	pipe.Set(ctx, keytree.VThreadSlot(vtid, 0, 0), "pre_main", 0)
	if _, err := pipe.Exec(ctx); err != nil {
		logx.Fatal("create vthread: %v", err)
	}

	logx.Info("[single] executing %s (%d ops)", vtid, len(body))
	vm.Execute(ctx, rdb, vtid)
	time.Sleep(3 * time.Second)

	vs := vthread.Get(ctx, rdb, vtid)
	fmt.Printf("\n=== VThread %s ===\n", vtid)
	fmt.Printf("  PC:     %s\n", vs.PC)
	fmt.Printf("  Status: %s\n", vs.Status)
	if vs.Error != nil {
		fmt.Printf("  Error:  %v\n", vs.Error)
	}
}

func collectKVFiles(path string) ([]string, error) {
	info, err := os.Stat(path)
	if err != nil {
		return nil, fmt.Errorf("stat %s: %w", path, err)
	}
	if !info.IsDir() {
		if strings.HasSuffix(path, ".kv") {
			return []string{path}, nil
		}
		return nil, fmt.Errorf("not a .kv file: %s", path)
	}
	var files []string
	err = filepath.Walk(path, func(p string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() && strings.HasSuffix(p, ".kv") {
			files = append(files, p)
		}
		return nil
	})
	return files, err
}
