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

	"kvlang/internal/logx"
	"kvlang/internal/op/builtin"
	"kvlang/internal/kvcpu"
	"kvlang/internal/keytree"

	"kvlang/internal/kvspace"
)

func cmdServe(args []string) {
	addr := "127.0.0.1:6379"
	if len(args) > 0 {
		addr = args[0]
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	vmID := os.Getenv("VM_ID")
	if vmID == "" {
		vmID = "0"
	}
	workers := runtime.GOMAXPROCS(0)
	logx.Info("VM-%s starting with %d workers, kv=%s", vmID, workers, addr)

	kv := kvspace.NewWithPool(addr, workers)
	defer kv.Close()

	if err := kv.Ping(ctx); err != nil {
		logx.Error("VM-%s kvspace connect failed: %v", vmID, err)
		os.Exit(1)
	}

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

	shutdownCtx, scancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer scancel()
	if err := kv.Del(shutdownCtx, keytree.SysVM(vmID)); err != nil {
		logx.Warn("VM-%s deregister failed: %v", vmID, err)
	}
	logx.Info("VM-%s shutdown complete", vmID)
}

func registerVM(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	reg := map[string]any{
		"status":     "running",
		"pid":        os.Getpid(),
		"started_at": time.Now().Unix(),
	}
	data, _ := json.Marshal(reg)
	if err := kv.Set(ctx, keytree.SysVM(vmID), data, 0); err != nil {
		logx.Error("VM-%s register failed: %v", vmID, err)
		os.Exit(1)
	}
	logx.Info("VM-%s registered at /sys/vm/%s", vmID, vmID)
}

func registerBuildinOps(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	key := keytree.OpBackendList("buildin")
	defs := builtin.OpDefs()
	kv.Del(ctx, key)
	for _, def := range defs {
		if err := kv.RPush(ctx, key, def); err != nil {
			logx.Error("VM-%s register op failed: %v", vmID, err)
			return
		}
	}
	logx.Info("VM-%s registered %d built-in ops", vmID, len(defs))
}

func heartbeatLoop(ctx context.Context, kv kvspace.KVSpace, vmID string) {
	key := keytree.SysHeartbeat(vmID)
	writeHB := func(status string) {
		hb := map[string]any{"ts": time.Now().Unix(), "status": status, "pid": os.Getpid()}
		data, _ := json.Marshal(hb)
		kv.Set(ctx, key, data, 0)
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
	const key = keytree.FuncMain
	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			val, err := kv.Get(ctx, key)
			if err != nil {
				continue
			}
			var entry struct {
				Entry  string   `json:"entry"`
				Reads  []string `json:"reads"`
				Writes []string `json:"writes"`
				Term   string   `json:"term,omitempty"`
			}
			if json.Unmarshal([]byte(val), &entry) != nil || entry.Entry == "" {
				continue
			}
			logx.Info("VM-%s /func/main entry=%s", vmID, entry.Entry)

			kv.Del(ctx, key) // atomic claim

			vtid, _ := kv.Incr(ctx, keytree.SysVtidCounter)
			vtidStr := fmt.Sprintf("%d", vtid)
			base := keytree.VThread(vtidStr)

			pipe := kv.Pipeline()
			pipe.Set(ctx, base, `{"pc":"[0,0]","status":"init"}`, 0)
			pipe.Set(ctx, base+"/[0,0]", entry.Entry, 0)
			for i, arg := range entry.Reads {
				pipe.Set(ctx, fmt.Sprintf("%s/[0,-%d]", base, i+1), arg, 0)
			}
			pipe.Set(ctx, base+"/[0,1]", "./ret", 0)
			if entry.Term != "" {
				pipe.Set(ctx, keytree.VThreadTerm(vtidStr), entry.Term, 0)
			}
			pipe.Exec(ctx)

			status, _ := json.Marshal(map[string]string{"vtid": vtidStr, "status": "executing"})
			kv.Set(ctx, key, status, 0)

			notify, _ := json.Marshal(map[string]any{"event": "new_vthread", "vtid": vtidStr})
			kv.LPush(ctx, keytree.NotifyVM, notify)
			logx.Info("VM-%s → vthread %s created", vmID, vtidStr)
		}
	}
}

func sysCmdListener(ctx context.Context, kv kvspace.KVSpace, vmID string, cancel context.CancelFunc) {
	queue := fmt.Sprintf("sys:cmd:vm:%s", vmID)
	for {
		result, err := kv.BLPop(ctx, 5*time.Second, queue)
		if err != nil {
			if ctx.Err() != nil {
				return
			}
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
