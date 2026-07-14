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
	"kvlang/internal/kvspace"
	"kvlang/internal/logx"
	"kvlang/internal/op/builtin"
	"kvlang/internal/vthread"
)

// executeEntry 创建 vthread 并同步执行 pre_main（单次模式）。
func executeEntry(kv kvspace.KVSpace) {
	ctx := context.Background()
	const vtid = "run"
	pc := keytree.VThreadSlot(vtid, "", 0, 0) // /vthread/run/[0,0]
	vthread.Set(ctx, kv, vtid, pc, "init")
	kv.Set(keytree.VThreadSlot(vtid, "", 0, 0), "pre_main")
	logx.Info("[single] executing %s", pc)
	cpu := kvcpu.New(kv, "single")
	// 直接执行，不走 pick/wait 队列
	cpu.Execute(pc)
}

// runServe 启动 VM daemon，持续监听并执行 vthread。
// args 为 serve 子命令的剩余参数（nil 或空切片均可）。
func runServe(args []string) {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	addr := fs.String("addr", "127.0.0.1:6379", "Redis 地址 (host:port)")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang serve [--addr host:port]")
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
	logx.Info("VM-%s starting with %d workers, kv=%s pool=%d", vmID, workers, *addr, poolSize)

	kv := kvspace.ConnPool(*addr, poolSize)
	defer kv.DisConn()
	registerDefaultTerm(kv)

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
	kv.Set(keytree.SysVM(vmID), data)
	logx.Info("VM-%s registered at %s", vmID, keytree.SysVM(vmID))
}

func registerBuildinOps(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	// 每个内置算子注册到 /sys/op/buildin/func/<opcode>
	for _, opcode := range builtin.NativeOpList() {
		kv.Set(keytree.SysOpFunc("buildin", opcode), "1")
	}
	// 注册实例状态：/sys/op/buildin/0
	kv.Set(keytree.SysOp("buildin", "0"), `{"status":"running","load":0}`)
	logx.Info("VM-%s registered buildin ops at %s", vmID, keytree.SysOpRoot)
}

func heartbeatLoop(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	key := keytree.SysVMHB(vmID)
	writeHB := func(status string) {
		hb := map[string]any{"ts": time.Now().Unix(), "status": status, "pid": os.Getpid()}
		data, _ := json.Marshal(hb)
		kv.Set(key, data)
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
			val, err := kv.Get(keytree.FuncMain)
			if err != nil { continue }
			var entry struct {
				Entry  string   `json:"entry"`
				Reads  []string `json:"reads"`
				Writes []string `json:"writes"`
				Term   string   `json:"term,omitempty"`
			}
			if json.Unmarshal([]byte(val), &entry) != nil || entry.Entry == "" { continue }
			logx.Info("VM-%s %s entry=%s", vmID, keytree.FuncMain, entry.Entry)
			kv.Del(keytree.FuncMain)

			vtidStr := incrVtid(kv)
			absPC := keytree.VThreadSlot(vtidStr, "", 0, 0)
			vthread.Set(ctx, kv, vtidStr, absPC, "init")
			kv.Set(keytree.VThreadSlot(vtidStr, "", 0, 0), entry.Entry)
			for i, arg := range entry.Reads {
				kv.Set(keytree.VThreadSlot(vtidStr, "", 0, -(i+1)), arg)
			}
			kv.Set(keytree.VThreadSlot(vtidStr, "", 0, 1), "./ret")
			if entry.Term != "" {
				kv.Set(keytree.VThreadTerm(vtidStr), entry.Term)
			}
			status, _ := json.Marshal(map[string]string{"vtid": vtidStr, "status": "executing"})
			kv.Set(keytree.FuncMain, status)
			kv.Notify(keytree.VthreadReady, vtidStr) // 平铺标量值，无 JSON
			logx.Info("VM-%s → vthread %s created pc=%s", vmID, vtidStr, absPC)
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
		if json.Unmarshal([]byte(result), &cmd) == nil && cmd.Cmd == "shutdown" {
			logx.Info("VM-%s sys shutdown", vmID)
			cancel()
			return
		}
	}
}
