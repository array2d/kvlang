package op_test

import (
	"context"
	"testing"

	"kvlang/internal/op"
	"kvlang/internal/op/dispatch"
	"kvlang/internal/kvspace"
)

// ── PC navigation ──

func TestNextPC(t *testing.T) {
	tests := []struct {
		pc   string
		want string
	}{
		{"[0,0]", "[1,0]"},
		{"[3,0]", "[4,0]"},
		{"[0,0]/[0,0]", "[0,0]/[1,0]"},
		{"[2,0]/[3,0]", "[2,0]/[4,0]"},
	}

	for _, tc := range tests {
		if got := op.NextPC(tc.pc); got != tc.want {
			t.Errorf("NextPC(%q) = %q, want %q", tc.pc, got, tc.want)
		}
	}
}

func TestParentPC(t *testing.T) {
	tests := []struct {
		pc   string
		want string
	}{
		{"[2,0]/[1,0]", "[3,0]"},
		{"[0,0]/[5,0]", "[1,0]"},
		{"[0,0]/[3,0]/[2,0]", "[0,0]/[4,0]"},
	}

	for _, tc := range tests {
		if got := op.ParentPC(tc.pc); got != tc.want {
			t.Errorf("ParentPC(%q) = %q, want %q", tc.pc, got, tc.want)
		}
	}
}

func TestIsTensorLifecycle(t *testing.T) {
	// tensor.* lifecycle ops (vtype 命名空间)
	lifecycle := []string{"tensor.new", "tensor.del", "tensor.clone"}
	notLifecycle := []string{"tensor.add", "tensor.matmul", "add", "matmul", "call", "return"}

	for _, opc := range lifecycle {
		if !op.IsTensorLifecycle(opc) {
			t.Errorf("IsTensorLifecycle(%q) = false, want true", opc)
		}
	}
	for _, opc := range notLifecycle {
		if op.IsTensorLifecycle(opc) {
			t.Errorf("IsTensorLifecycle(%q) = true, want false", opc)
		}
	}
}

func TestDecodeFromCache(t *testing.T) {
	cache := map[string]string{
		"[3,0]":  "add",
		"[3,-1]": "./a",
		"[3,-2]": "./b",
		"[3,1]":  "./c",
	}

	inst := op.DecodeFromCache(cache, "[3,0]")
	if inst.Opcode != "add" {
		t.Errorf("opcode = %q, want 'add'", inst.Opcode)
	}
	if len(inst.Reads) != 2 || inst.Reads[0] != "./a" || inst.Reads[1] != "./b" {
		t.Errorf("reads = %v, want [./a ./b]", inst.Reads)
	}
	if len(inst.Writes) != 1 || inst.Writes[0] != "./c" {
		t.Errorf("writes = %v, want [./c]", inst.Writes)
	}
}

// ── Route: error handling (no live kvspace needed) ──

func TestRouteSelect_NoKV(t *testing.T) {
	kv := kvspace.Conn("127.0.0.1:9999")
	ctx := context.Background()
	_, err := dispatch.Select(ctx, kv, "add")
	if err == nil {
		t.Error("expected error when KV is not available")
	}
	t.Logf("expected error: %v", err)
}
