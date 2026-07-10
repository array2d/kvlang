package parser_test

import (
	"testing"

	"kvlang/internal/ast"
	"kvlang/internal/lower"
	"kvlang/internal/parser"
)

func TestParse_ControlFlow(t *testing.T) {
	base := "../../example/kvlang/controlflow"

	parseAndLower := func(path string) *ast.File {
		t.Helper()
		df, err := parser.ParseFile(path)
		if err != nil {
			t.Fatalf("parse %s: %v", path, err)
		}
		return lower.File(df)
	}

	// countTopBlocks 统计函数体中顶层 BlockStmt 数量
	countTopBlocks := func(body []ast.Stmt) int {
		n := 0
		for _, st := range body {
			if _, ok := st.(*ast.BlockStmt); ok {
				n++
			}
		}
		return n
	}

	// allBlockBody 验证函数体是否全由 BlockStmt 组成
	allBlockBody := func(body []ast.Stmt) (blocks int, nonBlock int) {
		for _, st := range body {
			switch st.(type) {
			case *ast.BlockStmt:
				blocks++
			default:
				nonBlock++
			}
		}
		return
	}

	// ── if_else.kv ──
	t.Run("if_else", func(t *testing.T) {
		df := parseAndLower(base + "/if_else.kv")
		if len(df.Funcs) != 4 {
			t.Fatalf("expected 4 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			b, nb := allBlockBody(fn.Body)
			if nb > 0 {
				t.Errorf("[%s] %d non-block stmts, body should be all blocks after lowering", fn.Name, nb)
			}
			if b < 4 {
				t.Errorf("[%s] expected >=4 blocks, got %d", fn.Name, b)
			}
		}
	})

	// ── while_loop.kv ──
	t.Run("while_loop", func(t *testing.T) {
		df := parseAndLower(base + "/while_loop.kv")
		if len(df.Funcs) != 4 {
			t.Fatalf("expected 4 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			b, nb := allBlockBody(fn.Body)
			if nb > 0 {
				t.Errorf("[%s] %d non-block stmts", fn.Name, nb)
			}
			if b != 4 {
				t.Errorf("[%s] expected 4 blocks, got %d", fn.Name, b)
			}
		}
	})

	// ── block_branch.kv ──
	t.Run("block_branch", func(t *testing.T) {
		df := parseAndLower(base + "/block_branch.kv")
		if len(df.Funcs) != 2 {
			t.Fatalf("expected 2 funcs, got %d", len(df.Funcs))
		}
		if n := countTopBlocks(df.Funcs[0].Body); n != 4 {
			t.Errorf("[%s] expected 4 blocks, got %d", df.Funcs[0].Name, n)
		}
		if n := countTopBlocks(df.Funcs[1].Body); n != 5 {
			t.Errorf("[%s] expected 5 blocks, got %d", df.Funcs[1].Name, n)
		}
	})

	// ── block_loop.kv ──
	t.Run("block_loop", func(t *testing.T) {
		df := parseAndLower(base + "/block_loop.kv")
		if len(df.Funcs) != 2 {
			t.Fatalf("expected 2 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			if n := countTopBlocks(fn.Body); n != 5 {
				t.Errorf("[%s] expected 5 blocks, got %d", fn.Name, n)
			}
		}
	})

	// ── goto_chain.kv ──
	t.Run("goto_chain", func(t *testing.T) {
		df := parseAndLower(base + "/goto_chain.kv")
		if len(df.Funcs) != 2 {
			t.Fatalf("expected 2 funcs, got %d", len(df.Funcs))
		}
		if n := countTopBlocks(df.Funcs[0].Body); n != 5 {
			t.Errorf("[%s] expected 5 blocks, got %d", df.Funcs[0].Name, n)
		}
		if n := countTopBlocks(df.Funcs[1].Body); n != 5 {
			t.Errorf("[%s] expected 5 blocks, got %d", df.Funcs[1].Name, n)
		}
	})

	// ── tailrec_factorial.kv ──
	t.Run("tailrec_factorial", func(t *testing.T) {
		df := parseAndLower(base + "/tailrec_factorial.kv")
		if len(df.Funcs) != 2 {
			t.Fatalf("expected 2 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			b, nb := allBlockBody(fn.Body)
			if nb > 0 {
				t.Errorf("[%s] %d non-block stmts", fn.Name, nb)
			}
			if b < 4 {
				t.Errorf("[%s] expected >=4 blocks, got %d", fn.Name, b)
			}
		}
	})

	// ── tailrec_fib.kv ──
	t.Run("tailrec_fib", func(t *testing.T) {
		df := parseAndLower(base + "/tailrec_fib.kv")
		if len(df.Funcs) != 2 {
			t.Fatalf("expected 2 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			b, nb := allBlockBody(fn.Body)
			if nb > 0 {
				t.Errorf("[%s] %d non-block stmts", fn.Name, nb)
			}
			// lowered if/else chain produces >=4 blocks
			if b < 4 {
				t.Errorf("[%s] expected >=4 blocks, got %d", fn.Name, b)
			}
		}
	})

	// ── nested_control.kv ──
	t.Run("nested_control", func(t *testing.T) {
		df := parseAndLower(base + "/nested_control.kv")
		if len(df.Funcs) != 3 {
			t.Fatalf("expected 3 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			b, nb := allBlockBody(fn.Body)
			if nb > 0 {
				t.Errorf("[%s] %d non-block stmts", fn.Name, nb)
			}
			if b < 3 {
				t.Errorf("[%s] expected >=3 blocks, got %d", fn.Name, b)
			}
		}
	})

	// ── break_continue.kv ──
	t.Run("break_continue", func(t *testing.T) {
		df := parseAndLower(base + "/break_continue.kv")
		if len(df.Funcs) != 3 {
			t.Fatalf("expected 3 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			b, nb := allBlockBody(fn.Body)
			if nb > 0 {
				t.Errorf("[%s] %d non-block stmts", fn.Name, nb)
			}
			if b < 3 {
				t.Errorf("[%s] expected >=3 blocks, got %d", fn.Name, b)
			}
		}
	})

	// ── complex_flow.kv ──
	t.Run("complex_flow", func(t *testing.T) {
		df := parseAndLower(base + "/complex_flow.kv")
		if len(df.Funcs) != 3 {
			t.Fatalf("expected 3 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			b, nb := allBlockBody(fn.Body)
			if nb > 0 {
				t.Errorf("[%s] %d non-block stmts", fn.Name, nb)
			}
			if b < 3 {
				t.Errorf("[%s] expected >=3 blocks, got %d", fn.Name, b)
			}
		}
	})

	// ── call_graph_flow.kv ──
	t.Run("call_graph_flow", func(t *testing.T) {
		df := parseAndLower(base + "/call_graph_flow.kv")
		if len(df.Funcs) != 7 {
			t.Fatalf("expected 7 funcs, got %d", len(df.Funcs))
		}
		// is_positive, is_even: 1 instruction → 1 block
		for i := 0; i < 2; i++ {
			if n := countTopBlocks(df.Funcs[i].Body); n != 1 {
				t.Errorf("[%s] expected 1 block, got %d", df.Funcs[i].Name, n)
			}
		}
		// 递归函数: all blocks
		for i := 2; i < len(df.Funcs); i++ {
			b, nb := allBlockBody(df.Funcs[i].Body)
			if nb > 0 {
				t.Errorf("[%s] %d non-block stmts", df.Funcs[i].Name, nb)
			}
			if b < 3 {
				t.Errorf("[%s] expected >=3 blocks, got %d", df.Funcs[i].Name, b)
			}
		}
	})

	// ── for_loop.kv ──
	t.Run("for_loop", func(t *testing.T) {
		df := parseAndLower(base + "/for_loop.kv")
		if len(df.Funcs) != 3 {
			t.Fatalf("expected 3 funcs, got %d", len(df.Funcs))
		}
		for _, fn := range df.Funcs {
			b := countTopBlocks(fn.Body)
			if b < 1 {
				t.Errorf("[%s] expected >=1 blocks, got %d", fn.Name, b)
			}
		}
	})
}
