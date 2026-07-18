package parsertest

import (
	"fmt"
	"strings"
	"testing"

	"kvlang/internal/ast"
	"kvlang/internal/parser"
)

func TestPratt_SimpleArith(t *testing.T) {
	src := `
def add(A:int, B:int) -> (C:int) {
    A + B -> C
}
`
	f, diags, err := parser.ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	if len(diags) > 0 {
		t.Fatalf("diags: %v", diags)
	}
	fn := f.Funcs[0]
	if len(fn.Body) != 1 {
		t.Fatalf("expected 1 stmt, got %d", len(fn.Body))
	}
	inst := fn.Body[0].(*ast.Instruction)
	got := inst.String()
	if got != "A + B -> C" {
		t.Errorf("expected 'A + B -> C', got %q", got)
	}
}

func TestPratt_Precedence(t *testing.T) {
	// S3: a + b * c should parse as a + (b * c), not (a + b) * c
	src := `
def prec(A:int, B:int, C:int) -> (R:int) {
    A + B * C -> R
}
`
	f, _, err := parser.ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	inst := f.Funcs[0].Body[0].(*ast.Instruction)
	// Top-level op should be +
	if inst.Expr.Op != "+" {
		t.Errorf("expected top op '+', got %q", inst.Expr.Op)
	}
	// Right arg should be * expr
	if inst.Expr.Args[1].Op != "*" {
		t.Errorf("expected right arg '*', got %q", inst.Expr.Args[1].Op)
	}
	// String should be "A + B * C -> R" (no unnecessary parens)
	got := inst.String()
	if got != "A + B * C -> R" {
		t.Errorf("expected 'A + B * C -> R', got %q", got)
	}
}

func TestPratt_CompoundCond(t *testing.T) {
	// S3: compound condition in if
	src := `
def test(score:int) -> (R:int) {
    if (score > 90 && score < 100) {
        "A" -> R
    } else {
        "B" -> R
    }
}
`
	f, _, err := parser.ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	fn := f.Funcs[0]
	ifs := fn.Body[0].(*ast.IfStmt)
	// Cond should be &&(>(score,90), <(score,100))
	if ifs.Cond.Expr.Op != "&&" {
		t.Errorf("expected cond op '&&', got %q", ifs.Cond.Expr.Op)
	}
	condStr := ifs.Cond.String()
	fmt.Printf("  compound cond: %s\n", condStr)
}

func TestComments_Preserved(t *testing.T) {
	// S6: comments are preserved
	src := `
# This is the add function
def add(A:int, B:int) -> (C:int) {
    # compute sum
    A + B -> C
}

# top-level call
add(1, 2) -> result
`
	f, _, err := parser.ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	fn := f.Funcs[0]
	if len(fn.Comments) == 0 {
		t.Error("expected func to have leading comments")
	} else if fn.Comments[0] != "# This is the add function" {
		t.Errorf("unexpected func comment: %q", fn.Comments[0])
	}
	if len(fn.Body) == 0 {
		t.Fatal("empty body")
	}
	stmtComments := ast.StmtComments(fn.Body[0])
	if len(stmtComments) == 0 {
		t.Error("expected stmt to have leading comment")
	} else if stmtComments[0] != "# compute sum" {
		t.Errorf("unexpected stmt comment: %q", stmtComments[0])
	}
	if len(f.TopLevelCalls) == 0 {
		t.Fatal("no top-level calls")
	}
	callComments := f.TopLevelCalls[0].Comments
	if len(callComments) == 0 {
		t.Error("expected top-level call to have leading comment")
	} else if callComments[0] != "# top-level call" {
		t.Errorf("unexpected call comment: %q", callComments[0])
	}
}

func TestFormat_CommentsPreserved(t *testing.T) {
	// S6: Format() emits comments
	src := `
# This is the add function
def add(A:int, B:int) -> (C:int) {
    # compute sum
    A + B -> C
}
`
	f, _, err := parser.ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	var sb strings.Builder
	ast.Format(&sb, f)
	formatted := sb.String()
	if !strings.Contains(formatted, "# This is the add function") {
		t.Errorf("format lost function comment:\n%s", formatted)
	}
	if !strings.Contains(formatted, "# compute sum") {
		t.Errorf("format lost stmt comment:\n%s", formatted)
	}
	fmt.Printf("  formatted:\n%s\n", formatted)
}

func TestBreakContinue_Keywords(t *testing.T) {
	src := `
def loop_test(n:int) -> (R:int) {
    while (n > 0) {
        if (n > 10) {
            break
        }
        if (n > 5) {
            continue
        }
        n + 1 -> n
    }
    n -> R
}
`
	f, _, err := parser.ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	fn := f.Funcs[0]
	if len(fn.Body) < 2 {
		t.Fatalf("expected at least 2 body stmts, got %d", len(fn.Body))
	}
	t.Logf("parsed break/continue ok: %d stmts", len(fn.Body))
}

func TestScan_CommentTokens(t *testing.T) {
	toks := parser.Scan("# hello\nx + 1")
	found := false
	for _, tok := range toks {
		if tok.Kind == parser.Comment {
			found = true
			if tok.Value != "# hello" {
				t.Errorf("expected comment value '# hello', got %q", tok.Value)
			}
		}
	}
	if !found {
		t.Error("no Comment token produced")
	}
}

func TestScan_BreakContinue_Tokens(t *testing.T) {
	toks := parser.Scan("break continue")
	var kinds []parser.Kind
	for _, tok := range toks {
		if tok.Kind != parser.Newline && tok.Kind != parser.EOF {
			kinds = append(kinds, tok.Kind)
		}
	}
	if len(kinds) != 2 || kinds[0] != parser.Break || kinds[1] != parser.Continue {
		t.Errorf("expected [Break, Continue], got %v", kinds)
	}
}

func TestAssign_Eq(t *testing.T) {
	// = ≡ <-：写槽在左；== 不受影响；String() 保留 = 形态
	src := `
def f(A:int) -> (R:int) {
    x = A + 1
    ok = x == 2
    R = x
}
`
	f, diags, err := parser.ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	if len(diags) > 0 {
		t.Fatalf("diags: %v", diags)
	}
	body := f.Funcs[0].Body
	i0 := body[0].(*ast.Instruction)
	if !i0.ArrowLeft || !i0.Eq || len(i0.Writes) != 1 || i0.Writes[0] != "x" {
		t.Errorf("i0: ArrowLeft=%v Eq=%v Writes=%v", i0.ArrowLeft, i0.Eq, i0.Writes)
	}
	if got := i0.String(); got != "x = A + 1" {
		t.Errorf("expected 'x = A + 1', got %q", got)
	}
	if i1 := body[1].(*ast.Instruction); i1.Expr.Op != "==" {
		t.Errorf("expected '==' op, got %q", i1.Expr.Op)
	}
	if i2 := body[2].(*ast.Instruction); i2.Expr == nil || !i2.Expr.IsLeaf() || !i2.Eq {
		t.Errorf("i2: expected leaf copy with Eq")
	}
}
