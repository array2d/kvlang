package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"strings"

	"kvlang/internal/keytree"
	"kvlang/internal/kvcpu"
	"github.com/array2d/kvspace-go"
	"kvlang/internal/layoutrwir"
	"kvlang/internal/logx"
	"kvlang/internal/vthread"
)

// cmdRun 解析参数并路由：内联 / {lib}.{func} / 文件 / 管道。
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
		if !strings.HasSuffix(arg, ".kv") && strings.Contains(arg, keytree.FuncPathSep) {
			parts := strings.SplitN(arg, keytree.FuncPathSep, 2)
			runLib(parts[0], parts[1], *debug)
		} else if !strings.HasSuffix(arg, ".kv") {
			runLib(arg, "init", *debug)
		} else {
			runFiles(*dsn, fs.Args(), *debug)
		}
	case !isTerminal():
		runCode("stdin", os.Stdin, *dsn, *debug)
	default:
		runLib("", "init", false)
	}
}

// runLib 执行 /lib/{lib}.{func}。lib/func 为空时默认 "init"。
func runLib(lib, fn string, debug bool) {
	if fn == "" { fn = "init" }
	name := lib + keytree.FuncPathSep + fn
	if lib == "" { name = fn }
	kv := kvspace.Conn(defaultKVSpace())
	defer kv.DisConn()
	registerDefaultTerm(kv)
	executeEntry(kv, name, debug)
}

// executeEntry 创建 vthread 并同步执行。
func executeEntry(kv kvspace.KVSpace, entryName string, debug bool) {
	ctx := context.Background()
	vtid := incrVtid(kv)
	kv.DelTree(keytree.VThread(vtid))
	firstPC := layoutrwir.Bootstrap(ctx, kv, vtid, entryName, nil)
	if firstPC == "" {
		logx.Fatal("[single] Bootstrap %s failed", entryName)
	}
	vthread.Set(ctx, kv, vtid, firstPC, "init")
	kv.Set(keytree.VThreadTerm(vtid), kvspace.Str("kvlangrun"))

	if debug {
		kv.Set(keytree.VThreadDebugger(vtid), kvspace.Str("break"))
		logx.Info("[single] debug mode: executing %s", firstPC)
		cpu := kvcpu.New(kv, "single")
		cpu.Execute(firstPC)
		logx.Info("[dbg] execution finished")
		return
	}

	logx.Info("[single] executing %s", firstPC)
	cpu := kvcpu.New(kv, "single")
	cpu.Execute(firstPC)
	reportRunError(kv, vtid)
}

func reportRunError(kv kvspace.KVSpace, vtid string) {
	msgVal, err := kv.Get(keytree.VThreadStatusMsg(vtid, "error"))
	if err == nil && !msgVal.IsNil() {
		pcVal, _ := kv.Get(keytree.VThreadPC(vtid))
		logx.Error("%s at %s", msgVal.Str(), pcVal.Str())
		os.Exit(1)
	}
}
