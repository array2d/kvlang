package parser

import (
	"strings"
	"testing"
)

func TestParseTypeExpressionSignature(t *testing.T) {
	src := "rwfunc f(A:int64|float64, B:[2,3]float32, C:[?,768]float32) -> (D:[]float32) {\n    A -> D\n}\n"
	file, diags, err := ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("ParseCode: %v", err)
	}
	if HasErrors(diags) {
		t.Fatalf("unexpected errors: %v", diags)
	}
	sig := file.Funcs[0].Sig
	if sig.Name != "f" {
		t.Fatalf("name = %q", sig.Name)
	}
	var tys []string
	for _, p := range sig.Params {
		tys = append(tys, p.Type)
	}
	want := []string{"int64|float64", "[2,3]float32", "[?,768]float32"}
	if len(tys) != len(want) {
		t.Fatalf("params = %v, want %v", tys, want)
	}
	for i := range want {
		if tys[i] != want[i] {
			t.Fatalf("param[%d].Type = %q, want %q", i, tys[i], want[i])
		}
	}
	var rets []string
	for _, p := range sig.Returns {
		rets = append(rets, p.Type)
	}
	if len(rets) != 1 || rets[0] != "[]float32" {
		t.Fatalf("returns = %v", rets)
	}
}

func TestRejectMalformedTypeExpression(t *testing.T) {
	src := "rwfunc f(A:[2,3) -> () {\n}\n"
	_, diags, err := ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("ParseCode: %v", err)
	}
	if !HasErrors(diags) {
		t.Fatalf("expected errors for malformed type")
	}
}
