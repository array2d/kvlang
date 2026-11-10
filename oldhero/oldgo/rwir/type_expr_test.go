package rwir

import "testing"

func TestValidTypeExpr(t *testing.T) {
	valid := []string{
		"int64", "uint8", "float32", "bool", "char", "any",
		"char/utf8", "char/utf32", "char/ascii", "dict", "index", "extindex",
		"[]float32", "[2]float32", "[2,3]float32", "[2,3,4]float64",
		"[?,768]float32", "[?,?]int8",
		"int64|float64", "[2,3]float32|float32", "[]float32|[]float64",
		"bool|char", "index|dict", "[]float32|int64|bool",
	}
	for _, e := range valid {
		if !ValidTypeExpr(e) {
			t.Errorf("ValidTypeExpr(%q) = false, want true", e)
		}
	}

	invalid := []string{
		"", "int|", "|int", "int||float64", "|", "[]", "[2]", "[?]",
		"[2", "2]", "[2,]float32", "[,2]float32", "[2 3]float32",
		"*int64", "@int64", "int64*", "float64|",
		"int ", "float32,float64",
		"int", "uint", "float", "num", "int4", "fp8", "fp16", "string", "charbyte",
	}
	for _, e := range invalid {
		if ValidTypeExpr(e) {
			t.Errorf("ValidTypeExpr(%q) = true, want false", e)
		}
	}
}

func TestMatchType(t *testing.T) {
	cases := []struct {
		expr string
		kind string
		ndim int
		dims []int32
		want bool
	}{
		{"int64", "int64", 0, nil, true},
		{"int64", "float64", 0, nil, false},
		{"any", "dict", 0, nil, true},
		{"any", "int4", 0, nil, true},
		{"char", "char/utf8", 0, nil, true},
		{"char", "char/utf32", 0, nil, true},
		{"char", "int8", 0, nil, false},
		{"int64|float64", "float64", 0, nil, true},
		{"int64|float64", "bool", 0, nil, false},
		{"[]float32", "float32", 1, []int32{5}, true},
		{"[]float32", "float32", 0, nil, false},
		{"[2]float32", "float32", 1, []int32{2}, true},
		{"[2]float32", "float32", 1, []int32{3}, false},
		{"[2,3]float32", "float32", 2, []int32{2, 3}, true},
		{"[2,3]float32", "float32", 2, []int32{2, 4}, false},
		{"[2,3]float32", "float32", 1, []int32{2}, false},
		{"[?,768]float32", "float32", 2, []int32{100, 768}, true},
		{"[?,768]float32", "float32", 2, []int32{100, 512}, false},
		{"[2,3]float32|float32", "float32", 0, nil, true},
		{"[2,3]float32|float32", "float32", 2, []int32{2, 3}, true},
		{"[2,3]float32|float32", "float64", 0, nil, false},
		{"[]float32|[]float64", "float64", 1, []int32{10}, true},
		{"[]float32|[]float64", "int32", 1, []int32{10}, false},
		{"bool|char", "char/utf8", 0, nil, true},
		{"index|dict", "index", 0, nil, true},
	}
	for _, c := range cases {
		if got := MatchType(c.expr, c.kind, c.ndim, c.dims); got != c.want {
			t.Errorf("MatchType(%q, %q, ndim=%d, dims=%v) = %v, want %v",
				c.expr, c.kind, c.ndim, c.dims, got, c.want)
		}
	}
}
