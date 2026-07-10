package main

import (
	"context"
	"encoding/json"
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
	st := vthread.VThread{PC: "[0,0]", Status: "init", Mode: "single"}
	data, _ := json.Marshal(st)
	kv.Set(keytree.VThread("run"), data, 0)
	kv.Set(keytree.VThreadSlot("run", 0, 0), "pre_main", 0)
	logx.Info("[single] executing run")
	kvcpu.Execute(context.Background(), kv, "run")
}

// runServe 启动 VM daemon，持续监听并执行 vthread。
func runServe() {
	addr := "127.0.0.1:6379"
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	vmID := os.Getenv("VM_ID")
	if vmID == "" { vmID = "0" }
	workers := runtime.GOMAXPROCS(0)
	// 每个 worker 最多占用 1 个连接做 BLPOP，额外 16 用于 mainWatcher/heartbeat/syscmd 等。
	poolSize := workers + 16
	logx.Info("VM-%s starting with %d workers, kv=%s pool=%d", vmID, workers, addr, poolSize)

	kv := kvspace.ConnPool(addr, poolSize)
	defer kv.DisConn()
	registerDefaultTerm(kv)

	registerVM(ctx, kv, vmID)
	registerBuildinOps(ctx, kv, vmID)

	for i := 0; i < workers; i++ {
		go kvcpu.RunWorker(ctx, kv, i)
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
	kv.Set(keytree.SysVM(vmID), data, 0)
	logx.Info("VM-%s registered at %s", vmID, keytree.SysVM(vmID))
}

func registerBuildinOps(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	key := keytree.OpBackendList("buildin")
	defs := builtin.OpDefs()
	kv.Del(key)
	for _, def := range defs {
		kv.Notify(key, def)
	}
	logx.Info("VM-%s registered %d built-in ops", vmID, len(defs))
}

func heartbeatLoop(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	key := keytree.SysHeartbeat(vmID)
	writeHB := func(status string) {
		hb := map[string]any{"ts": time.Now().Unix(), "status": status, "pid": os.Getpid()}
		data, _ := json.Marshal(hb)
		kv.Set(key, data, 0)
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
			base := keytree.VThread(vtidStr)
			kv.Set(base, `{"pc":"[0,0]","status":"init"}`, 0)
			kv.Set(base+"/[0,0]", entry.Entry, 0)
			for i, arg := range entry.Reads {
				kv.Set(fmt.Sprintf("%s/[0,-%d]", base, i+1), arg, 0)
			}
			kv.Set(base+"/[0,1]", "./ret", 0)
			if entry.Term != "" {
				kv.Set(keytree.VThreadTerm(vtidStr), entry.Term, 0)
			}
			status, _ := json.Marshal(map[string]string{"vtid": vtidStr, "status": "executing"})
			kv.Set(keytree.FuncMain, status, 0)
			notify, _ := json.Marshal(map[string]any{"event": "new_vthread", "vtid": vtidStr})
			kv.Notify(keytree.NotifyVM, notify)
			logx.Info("VM-%s → vthread %s created", vmID, vtidStr)
		}
	}
}

func sysCmdListener(ctx context.Context, kv kvspace.KVSpace, vmID string, cancel context.CancelFunc) {
	queue := keytree.SysCmdVM(vmID)
	for {
		result, err := kv.Watch(5*time.Second, queue)
		if err != nil {
			if ctx.Err() != nil { return }
			continue
		}
		var cmd struct{ Cmd string `json:"cmd"` }
		if json.Unmarshal([]byte(result[1]), &cmd) == nil && cmd.Cmd == "shutdown" {
			logx.Info("VM-%s sys shutdown", vmID)
			cancel()
			return
		}
	}
}

