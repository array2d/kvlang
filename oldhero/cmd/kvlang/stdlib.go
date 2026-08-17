package main

import (
	"context"
	"strings"

	"oldhero/keytree"
	"oldhero/kvcpu"
	"github.com/array2d/kvspace-go"
	"oldhero/layout"
	"oldhero/logx"
	"oldhero/lower"
	"oldhero/parser"
	"oldhero/stdlib"
	"oldhero/vthread"
)

// layoutAndRunStdlib 在 runtime 启动时 layout 内置 lib 源码到 /lib/，并 run 各 lib 的 init。
// 每个 init 在独立 vthread 执行，完成后回收 /vthread/<vtid>/，vtid 永远递增。
func layoutAndRunStdlib(kv kvspace.KVSpace) {
	entries, err := stdlib.FS.ReadDir(".")
	if err != nil {
		return
	}
	var inits []string
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".kv") {
			continue
		}
		src, err := stdlib.FS.ReadFile(e.Name())
		if err != nil {
			continue
		}
		df, diags, err := parser.ParseCode(strings.NewReader(string(src)))
		if err != nil {
			logx.Fatal("stdlib parse %s: %v", e.Name(), err)
		}
		for _, d := range diags {
			d.SrcName = "stdlib/" + e.Name()
			logx.Diag(d)
		}
		if parser.HasErrors(diags) {
			logx.Fatal("stdlib parse %s: error-level diagnostics", e.Name())
		}
		for i := range df.Funcs {
			fpkg := df.Funcs[i].Pkg
			if fpkg == "" {
				fpkg = df.Package
			}
			layout.WriteFunc(kv, fpkg, lower.Func(&df.Funcs[i]))
			if df.Funcs[i].Sig.Name == "init" && fpkg != "" {
				inits = append(inits, fpkg+keytree.MemberSep+"init")
			}
		}
	}
	for _, fn := range inits {
		runStdlibInit(kv, fn)
	}
}

// runStdlibInit 在独立 vthread 执行 init，完成后回收 /vthread/<vtid>/（vtid 不回收）。
func runStdlibInit(kv kvspace.KVSpace, funcName string) {
	ctx := context.Background()
	vtid := vthread.AllocVtid(kv)
	kvspace.MkIndexRecursive(kv, keytree.VThread(vtid)+"/")
	firstPC := layout.Bootstrap(ctx, kv, vtid, funcName, nil)
	if firstPC == "" {
		kv.DelTree(keytree.VThread(vtid))
		return
	}
	vthread.Set(ctx, kv, vtid, firstPC, "init")
	cpu := kvcpu.New(kv)
	cpu.Execute(firstPC)
	kv.DelTree(keytree.VThread(vtid))
	// 删除已执行的 init 函数树，避免 findEntry 把 stdlib 的 lib init 误当用户入口
	if dot := strings.LastIndex(funcName, keytree.MemberSep); dot > 0 {
		kv.DelTree(keytree.LibFunc(funcName[:dot], funcName[dot+len(keytree.MemberSep):]))
	}
}
