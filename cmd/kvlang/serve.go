package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"runtime"
	"syscall"
	"time"

	"kvlang/internal/keytree"
	"kvlang/internal/kvcpu"
	"github.com/array2d/kvlang-go"
	"kvlang/internal/layoutcode"
	"kvlang/internal/logx"
	"kvlang/internal/op/builtin"
	"kvlang/internal/vthread"
)

// executeEntry 创建 vthread 并同步执行 init（单次模式）。
//
// debug=true 时在执行前设置 .debug="step"，启动交互式单步调试代理。
func executeEntry(kv kvspace.KVSpace, debug bool) {
	ctx := context.Background()
	const vtid = "run"
	kv.DelTree(keytree.VThread(vtid)) // 清上次运行的整棵 vthread 残留（帧变量/错误态），防跨次污染
	firstPC := layoutcode.Bootstrap(ctx, kv, vtid, "init", nil)
	if firstPC == "" {
		logx.Fatal("[single] Bootstrap init failed")
	}
	vthread.Set(ctx, kv, vtid, firstPC, "init")
	// 自动绑定进程标准终端
	kv.Set(keytree.VThreadTerm(vtid), kvspace.Str("kvlangrun"))

	if debug {
		// 在执行开始前设置单步标志，CPU 会在第一条函数入口处检测到并暂停
		kv.Set(keytree.VThreadDebug(vtid), kvspace.Str("step"))
		done := make(chan struct{})
		go runDebugAgent(kv, vtid, done)
		logx.Info("[single] debug mode: executing %s", firstPC)
		cpu := kvcpu.New(kv, "single")
		cpu.Execute(firstPC)
		close(done)
		fmt.Fprintln(os.Stderr, "\n[dbg] execution finished")
		return
	}

	logx.Info("[single] executing %s", firstPC)
	cpu := kvcpu.New(kv, "single")
	cpu.Execute(firstPC)
	reportRunError(kv, vtid)
}

// reportRunError 单次模式终态 error 回显 stderr 并以非零退出（fix-016）。
func reportRunError(kv kvspace.KVSpace, vtid string) {
	msgVal, err := kv.Get(keytree.VThreadStatusMsg(vtid, "error"))
	if err == nil && !msgVal.IsNil() {
		pcVal, _ := kv.Get(keytree.VThreadPC(vtid))
		fmt.Fprintln(os.Stderr, "error:", msgVal.Str(), "at", pcVal.Str())
		os.Exit(1)
	}
}

// runServe 启动 VM daemon，持续监听并执行 vthread。
// args 为 serve 子命令的剩余参数（nil 或空切片均可）。
func runServe(args []string) {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	dsn := fs.String("kvspace", defaultKVSpace(), kvspaceFlagDesc)
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang serve [--kvspace dsn]")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	vmID := os.Getenv("VM_ID")
	if vmID == "" { vmID = "0" }
	workers := runtime.GOMAXPROCS(0)
	// 每个 worker 最多占用 1 个连接做 BLPOP，额外 16 用于 mainWatcher/heartbeat/syscmd 等。
	poolSize := workers + 16
	logx.Info("VM-%s starting with %d workers, kv=%s pool=%d", vmID, workers, *dsn, poolSize)

	kv := kvspace.ConnPool(*dsn, poolSize)
	defer kv.DisConn()
	// serve 是 daemon，不注册 kvlangrun 终端——
	// 各 vthread 的终端由 agent 通过 entry.Term 显式绑定。

	registerVM(ctx, kv, vmID)
	registerBuildinOps(ctx, kv, vmID)

	c := kvcpu.New(kv, vmID)
	for i := 0; i < workers; i++ {
		go c.RunWorker(i)
	}
	logx.Info("VM-%s %d workers started", vmID, workers)

	go heartbeatLoop(ctx, kv, vmID)
	go mainWatcher(ctx, kv, vmID)
	go sysCmdListener(ctx, kv, vmID, cancel)

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	select {
	case s := <-sig:
		logx.Info("VM-%s received %s, shutting down...", vmID, s)
	case <-ctx.Done():
		logx.Info("VM-%s context cancelled, shutting down...", vmID)
	}
	cancel()
	kv.Del(keytree.SysVM(vmID))
	logx.Info("VM-%s shutdown complete", vmID)
}

func registerVM(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	reg := map[string]any{"status": "running", "pid": os.Getpid(), "started_at": time.Now().Unix()}
	data, _ := json.Marshal(reg)
	kv.Set(keytree.SysVM(vmID), kvspace.Bytes(data))
	logx.Info("VM-%s registered at %s", vmID, keytree.SysVM(vmID))
}

func registerBuildinOps(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	// 每个内置算子注册到 /sys/op/buildin/func/<opcode>
	for _, opcode := range builtin.NativeOpList() {
		kv.Set(keytree.SysOpFunc("buildin", opcode), kvspace.Str("1"))
	}
	// 注册实例状态：/sys/op/buildin/0
	kv.Set(keytree.SysOp("buildin", "0"), kvspace.Str(`{"status":"running","load":0}`))
	logx.Info("VM-%s registered buildin ops at %s", vmID, keytree.SysOpRoot)
}

func heartbeatLoop(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	key := keytree.SysVMHB(vmID)
	writeHB := func(status string) {
		hb := map[string]any{"ts": time.Now().Unix(), "status": status, "pid": os.Getpid()}
		data, _ := json.Marshal(hb)
		kv.Set(key, kvspace.Bytes(data))
	}
	writeHB("running")
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			writeHB("stopped")
			return
		case <-ticker.C:
			writeHB("running")
		}
	}
}

func mainWatcher(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			entryVal, err := kv.Get(keytree.FuncMain)
			if err != nil { continue }
			var entry struct {
				Entry  string   `json:"entry"`
				Reads  []string `json:"reads"`
				Writes []string `json:"writes"`
				Term   string   `json:"term,omitempty"`
			}
			if json.Unmarshal([]byte(entryVal.Str()), &entry) != nil || entry.Entry == "" { continue }
			logx.Info("VM-%s %s entry=%s", vmID, keytree.FuncMain, entry.Entry)
			kv.Del(keytree.FuncMain)

			vtidStr := incrVtid(kv)
			firstPC := layoutcode.Bootstrap(ctx, kv, vtidStr, entry.Entry, entry.Reads)
			if firstPC == "" {
				logx.Warn("VM-%s Bootstrap %s failed", vmID, entry.Entry)
				continue
			}
			vthread.Set(ctx, kv, vtidStr, firstPC, "init")
			if entry.Term != "" {
				kv.Set(keytree.VThreadTerm(vtidStr), kvspace.Str(entry.Term))
			}
			status, _ := json.Marshal(map[string]string{"vtid": vtidStr, "status": "executing"})
			kv.Set(keytree.FuncMain, kvspace.Bytes(status))
			kv.Notify(keytree.VthreadReady, kvspace.Str(vtidStr))
			logx.Info("VM-%s → vthread %s created pc=%s", vmID, vtidStr, firstPC)
		}
	}
}

func sysCmdListener(ctx context.Context, kv kvspace.KVSpace, vmID string, cancel context.CancelFunc) {
	queue := keytree.SysVMCmd(vmID)
	for {
		result, err := kv.Watch(queue, 5*time.Second)
		if err != nil {
			if ctx.Err() != nil { return }
			continue
		}
		var cmd struct{ Cmd string `json:"cmd"` }
		if json.Unmarshal([]byte(result.Str()), &cmd) == nil && cmd.Cmd == "shutdown" {
			logx.Info("VM-%s sys shutdown", vmID)
			cancel()
			return
		}
	}
}
