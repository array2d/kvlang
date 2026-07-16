package parser

import (
	"strings"
	"testing"
)

func TestScanNumeric_ScientificNotation(t *testing.T) {
	tests := []struct {
		src  string
		want string
	}{
		// Standard scientific notation — must be single tokens
		{"1e10", "1e10"},
		{"1e+10", "1e+10"},
		{"1e-10", "1e-10"},
		{"1E10", "1E10"},
		{"3.14e5", "3.14e5"},
		{"3.14e+5", "3.14e+5"},
		{"3.14e-5", "3.14e-5"},
		{"1.5e-3", "1.5e-3"},
		// Normal numbers
		{"42", "42"},
		{"0", "0"},
		{"3.14", "3.14"},
		// Edge cases — partial "e" is a single token (runtime handles invalid)
		{"1e", "1e"},
		{"42e+", "42e+"},
		{"3.14e", "3.14e"},
	}

	for _, tc := range tests {
		tc := tc
		t.Run(tc.src, func(t *testing.T) {
			tokens := Scan(tc.src)
			var vals []string
			for _, tok := range tokens {
				if tok.Kind != EOF {
					vals = append(vals, tok.Value)
				}
			}
			got := strings.Join(vals, " ")
			if got != tc.want {
				t.Errorf("got %q, want %q", got, tc.want)
			}
		})
	}
}

func TestScanNumeric_InExpression(t *testing.T) {
	tests := []struct {
		src  string
		want string
	}{
		// Unary minus + scientific → two tokens (minus is separate operator)
		{"-1e+10", "- 1e+10"},
		{"-3.14e-5", "- 3.14e-5"},
		// Scientific in expression
		{"3.14e-5 + 2", "3.14e-5 + 2"},
		// Arrow expression
		{"1e+10 -> ./x", "1e+10 -> ./x"},
	}

	for _, tc := range tests {
		tc := tc
		t.Run(tc.src, func(t *testing.T) {
			tokens := Scan(tc.src)
			var vals []string
			for _, tok := range tokens {
				if tok.Kind != EOF && tok.Kind != Newline {
					vals = append(vals, tok.Value)
				}
			}
			got := strings.Join(vals, " ")
			if got != tc.want {
				t.Errorf("got %q, want %q", got, tc.want)
			}
		})
	}
}

func TestParseNumericLiteral_InvalidProducesDiagnostic(t *testing.T) {
	tests := []struct {
		src          string
		wantContains string
	}{
		// 对标 Go: "exponent has no digits"
		{"def f()->(){ 1e -> ./x }", "invalid numeric literal"},
		{"def f()->(){ 42e+ -> ./x }", "invalid numeric literal"},
		{"def f()->(){ 3.14e -> ./x }", "invalid numeric literal"},
		{"def f()->(){ 1E- -> ./x }", "invalid numeric literal"},
		// Valid numbers should NOT produce diagnostics
	}
	for _, tc := range tests {
		tc := tc
		t.Run(tc.src, func(t *testing.T) {
			_, diags, err := ParseCode(strings.NewReader(tc.src))
			if err != nil {
				t.Fatalf("ParseCode error: %v", err)
			}
			found := false
			for _, d := range diags {
				if strings.Contains(d.Message, tc.wantContains) {
					found = true
					t.Logf("diag: %s", d.Message)
				}
			}
			if !found {
				t.Errorf("no diagnostic containing %q in %v", tc.wantContains, diags)
			}
		})
	}
}

func TestParseNumericLiteral_ValidNoDiagnostic(t *testing.T) {
	tests := []string{
		"def f()->(){ 1e10 -> ./x }",
		"def f()->(){ 1e+10 -> ./x }",
		"def f()->(){ 3.14e-5 -> ./x }",
		"def f()->(){ 42 -> ./x }",
		"def f()->(){ -7 -> ./x }",
		"def f()->(){ 0 -> ./x }",
	}
	for _, src := range tests {
		src := src
		t.Run(src, func(t *testing.T) {
			_, diags, err := ParseCode(strings.NewReader(src))
			if err != nil {
				t.Fatalf("ParseCode error: %v", err)
			}
			for _, d := range diags {
				if strings.Contains(d.Message, "invalid numeric") {
					t.Errorf("unexpected numeric diagnostic: %s", d.Message)
				}
			}
		})
	}
}
