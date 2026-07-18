package parsertest

import (
	"fmt"
	"strings"
	"testing"

	"kvlang/internal/parser"
)

func TestSemicolon_StatementSeparator(t *testing.T) {
	src := `1 + 2 + 3 -> x; print("x =", x)`
	tokens := parser.Scan(src)
	for _, tok := range tokens {
		fmt.Printf("  %s\n", tok)
	}
	fmt.Println("---")
	r := strings.NewReader(src)
	f, diags, err := parser.ParseCode(r)
	if err != nil {
		t.Fatalf("ParseCode error: %v", err)
	}
	for _, d := range diags {
		t.Logf("diag: %s", d)
	}
	if f == nil {
		t.Fatal("nil file")
	}
	fmt.Printf("Funcs=%d TopLevelCalls=%d\n", len(f.Funcs), len(f.TopLevelCalls))
	for i, call := range f.TopLevelCalls {
		fmt.Printf("  call[%d]: Expr=%v Writes=%v\n", i, call.Expr, call.Writes)
	}
	if len(f.TopLevelCalls) != 2 {
		t.Errorf("expected 2 TopLevelCalls, got %d", len(f.TopLevelCalls))
	}
	// instruction 1: writes should be ["x"] only
	if got := f.TopLevelCalls[0].Writes; len(got) != 1 || got[0] != "x" {
		t.Errorf("call[0] writes: expected [x], got %v", got)
	}
	// instruction 2: print call, no writes
	if got := f.TopLevelCalls[1].Writes; len(got) != 0 {
		t.Errorf("call[1] writes: expected [], got %v", got)
	}
}
