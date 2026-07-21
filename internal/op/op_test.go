package op_test

import (
	"testing"

	"kvlang/internal/op"
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

func TestWriteSlotPC(t *testing.T) {
	tests := []struct {
		pc   string
		slot int
		want string
	}{
		// 调用指令 [3,0]，写槽 0 → [3,1]，写槽 1 → [3,2]
		{"/vthread/42/.fn/[3,0]", 0, "/vthread/42/.fn/[3,1]"},
		{"/vthread/42/.fn/[3,0]", 1, "/vthread/42/.fn/[3,2]"},
		// 嵌套帧（子帧的调用指令）
		{"/vthread/42/[0,0]/.fn/[5,0]", 0, "/vthread/42/[0,0]/.fn/[5,1]"},
		// 指令 0
		{"/vthread/42/.fn/[0,0]", 0, "/vthread/42/.fn/[0,1]"},
	}

	for _, tc := range tests {
		if got := op.WriteSlotPC(tc.pc, tc.slot); got != tc.want {
			t.Errorf("WriteSlotPC(%q, %d) = %q, want %q", tc.pc, tc.slot, got, tc.want)
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
