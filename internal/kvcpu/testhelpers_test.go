package kvcpu_test

import (
	"context"
	"strings"
	"encoding/json"
	"os"
	"testing"
	"time"

	"kvlang/internal/kvspace"
)

// connectKVSpace connects to kvspace for integration tests.
// Uses KV_ADDR env or defaults to 127.0.0.1:6379.
func connectKVSpace(t *testing.T) (kvspace.KVSpace, context.Context) {
	t.Helper()
	addr := os.Getenv("KV_ADDR")
	if addr == "" {
		addr = "127.0.0.1:16379"
	}
	ctx := context.Background()
	kv := kvspace.Conn(addr)
	return kv, context.Background()
}

// waitVthreadDone polls the vthread state until it reaches "done" or "error".
// Returns named slot values on success.
func waitVthreadDone(t *testing.T, kv kvspace.KVSpace, vtid string, timeout time.Duration) (map[string]string, bool) {
	t.Helper()
	ticker := time.NewTicker(30 * time.Millisecond)
	defer ticker.Stop()
	deadline := time.Now().Add(timeout)

	for time.Now().Before(deadline) {
		<-ticker.C
		val, err := kv.Get("/vthread/"+vtid)
		if err != nil && strings.Contains(err.Error(), "nil") {
			continue
		}
		if err != nil {
			continue
		}
		var s struct {
			Status string            `json:"status"`
			PC     string            `json:"pc"`
			Error  map[string]string `json:"error,omitempty"`
		}
		json.Unmarshal([]byte(val), &s)

		switch s.Status {
		case "done":
			// read named slots
			keys, _ := kv.Keys("/vthread/"+vtid+"/*")
			outputs := make(map[string]string)
			prefix := "/vthread/" + vtid + "/"
			for _, k := range keys {
				if v, err := kv.Get(k); err == nil {
					slot := k[len(prefix):]
					if len(slot) > 0 && slot[0] != '[' {
						outputs[slot] = v
					}
				}
			}
			return outputs, true
		case "error":
			t.Logf("vtid=%s error: %v", vtid, s.Error)
			return nil, false
		}
	}
	t.Logf("vtid=%s timeout after %v", vtid, timeout)
	return nil, false
}