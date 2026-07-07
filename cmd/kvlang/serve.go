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
	"kvlang/internal/vm"

	"github.com/redis/go-redis/v9"
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
	logx.Info("VM-%s starting with %d workers, redis=%s", vmID, workers, addr)

	rdb := redis.NewClient(&redis.Options{
		Addr:         addr,
		PoolSize:     workers * 2,
		MinIdleConns: workers,
		PoolTimeout:  10 * time.Second,
		ReadTimeout:  3 * time.Second,
		WriteTimeout: 3 * time.Second,
	})
	defer rdb.Close()

	if err := rdb.Ping(ctx).Err(); err != nil {
		logx.Error("VM-%s redis connect failed: %v", vmID, err)
		os.Exit(1)
	}

	registerVM(ctx, rdb, vmID)
	registerBuildinOps(ctx, rdb, vmID)

	for i := 0; i < workers; i++ {
		go vm.RunWorker(ctx, rdb, i)
	}
	logx.Info("VM-%s %d workers started", vmID, workers)

	go heartbeatLoop(ctx, rdb, vmID)
	go mainWatcher(ctx, rdb, vmID)
	go sysCmdListener(ctx, rdb, vmID, cancel)

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
	if err := rdb.Del(shutdownCtx, "/sys/vm/"+vmID).Err(); err != nil {
		logx.Warn("VM-%s deregister failed: %v", vmID, err)
	}
	logx.Info("VM-%s shutdown complete", vmID)
}

func registerVM(ctx context.Context, rdb *redis.Client, vmID string) {
	reg := map[string]any{
		"status":     "running",
		"pid":        os.Getpid(),
		"started_at": time.Now().Unix(),
	}
	data, _ := json.Marshal(reg)
	if err := rdb.Set(ctx, "/sys/vm/"+vmID, data, 0).Err(); err != nil {
		logx.Error("VM-%s register failed: %v", vmID, err)
		os.Exit(1)
	}
	logx.Info("VM-%s registered at /sys/vm/%s", vmID, vmID)
}

func registerBuildinOps(ctx context.Context, rdb *redis.Client, vmID string) {
	const key = "/op/buildin/list"
	defs := builtin.OpDefs()
	rdb.Del(ctx, key)
	for _, def := range defs {
		if err := rdb.RPush(ctx, key, def).Err(); err != nil {
			logx.Error("VM-%s register op failed: %v", vmID, err)
			return
		}
	}
	logx.Info("VM-%s registered %d built-in ops", vmID, len(defs))
}

func heartbeatLoop(ctx context.Context, rdb *redis.Client, vmID string) {
	key := fmt.Sprintf("/sys/heartbeat/vm:%s", vmID)
	writeHB := func(status string) {
		hb := map[string]any{"ts": time.Now().Unix(), "status": status, "pid": os.Getpid()}
		data, _ := json.Marshal(hb)
		rdb.Set(ctx, key, data, 0)
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

func mainWatcher(ctx context.Context, rdb *redis.Client, vmID string) {
	const key = "/func/main"
	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			val, err := rdb.Get(ctx, key).Result()
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

			rdb.Del(ctx, key) // atomic claim

			vtid, _ := rdb.Incr(ctx, "/sys/vtid_counter").Result()
			vtidStr := fmt.Sprintf("%d", vtid)
			base := "/vthread/" + vtidStr

			pipe := rdb.Pipeline()
			pipe.Set(ctx, base, `{"pc":"[0,0]","status":"init"}`, 0)
			pipe.Set(ctx, base+"/[0,0]", entry.Entry, 0)
			for i, arg := range entry.Reads {
				pipe.Set(ctx, fmt.Sprintf("%s/[0,-%d]", base, i+1), arg, 0)
			}
			pipe.Set(ctx, base+"/[0,1]", "./ret", 0)
			if entry.Term != "" {
				pipe.Set(ctx, base+"/term", entry.Term, 0)
			}
			pipe.Exec(ctx)

			status, _ := json.Marshal(map[string]string{"vtid": vtidStr, "status": "executing"})
			rdb.Set(ctx, key, status, 0)

			notify, _ := json.Marshal(map[string]any{"event": "new_vthread", "vtid": vtidStr})
			rdb.LPush(ctx, "notify:vm", notify)
			logx.Info("VM-%s → vthread %s created", vmID, vtidStr)
		}
	}
}

func sysCmdListener(ctx context.Context, rdb *redis.Client, vmID string, cancel context.CancelFunc) {
	queue := fmt.Sprintf("sys:cmd:vm:%s", vmID)
	for {
		result, err := rdb.BLPop(ctx, 5*time.Second, queue).Result()
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
