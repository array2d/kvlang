package parser_test

import (
	"fmt"
	"testing"

	"kvlang/internal/parser"
	"kvlang/internal/ast"
)

// loadFirstFunc parses a .kv file and returns its first function.
func loadFirstFunc(path string) (*ast.Func, error) {
	df, err := parser.ParseFile(path)
	if err != nil {
		return nil, err
	}
	if len(df.Funcs) == 0 {
		return nil, fmt.Errorf("no functions in %s", path)
	}
	return &df.Funcs[0], nil
}

type wantInst struct {
	op     string
	reads  []string
	writes []string
}

// verifyInst 验证一条指令的解析结果
func verifyInst(t *testing.T, dxFile string, lineIdx int, inst *ast.Instruction, want wantInst) {
	t.Helper()
	if inst.Opcode != want.op {
		t.Errorf("[%s] line[%d] opcode=%s, want %s", dxFile, lineIdx, inst.Opcode, want.op)
	}
	if len(inst.Reads) != len(want.reads) {
		t.Errorf("[%s] line[%d] reads len=%d, want %d (%v vs %v)", dxFile, lineIdx, len(inst.Reads), len(want.reads), inst.Reads, want.reads)
		return
	}
	for i := range inst.Reads {
		if inst.Reads[i] != want.reads[i] {
			t.Errorf("[%s] line[%d] reads[%d]=%s, want %s", dxFile, lineIdx, i, inst.Reads[i], want.reads[i])
		}
	}
	if len(inst.Writes) != len(want.writes) {
		t.Errorf("[%s] line[%d] writes len=%d, want %d (%v vs %v)", dxFile, lineIdx, len(inst.Writes), len(want.writes), inst.Writes, want.writes)
		return
	}
	for i := range inst.Writes {
		if inst.Writes[i] != want.writes[i] {
			t.Errorf("[%s] line[%d] writes[%d]=%s, want %s", dxFile, lineIdx, i, inst.Writes[i], want.writes[i])
		}
	}
}

// checkKv 加载 .kv 文件并逐行验证解析结果
func checkKv(t *testing.T, dxFile string, wants []wantInst) {
	t.Helper()
	fn, err := loadFirstFunc(dxFile)
	if err != nil {
		t.Fatalf("LoadDxFile(%s): %v", dxFile, err)
	}
	if len(fn.Body) != len(wants) {
		t.Fatalf("[%s] body has %d lines, want %d:\n  got:  %v\n  want: %v", dxFile, len(fn.Body), len(wants), fn.Body, wants)
	}
	for i, w := range wants {
		inst, err := parser.ParseLine(fn.Body[i].String())
		if err != nil {
			t.Errorf("[%s] line[%d] parse error: %v", dxFile, i, err)
			continue
		}
		verifyInst(t, dxFile, i, inst, w)
	}
}

// ── Lifecycle ───────────────────────────────────────────────

func TestParse_Lifecycle(t *testing.T) {
	t.Run("newtensor", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/tensor/lifecycle/newtensor.kv", []wantInst{
			{op: "newtensor", reads: []string{"f32", "[16]"}, writes: []string{"/data/x"}},
		})
	})
	t.Run("del", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/tensor/lifecycle/del.kv", []wantInst{
			{op: "newtensor", reads: []string{"f32", "[8]"}, writes: []string{"/data/tmp"}},
			{op: "deltensor", reads: []string{"/data/tmp"}, writes: nil},
		})
	})
	t.Run("compute_small", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/tensor/lifecycle/compute.kv", []wantInst{
			{op: "newtensor", reads: []string{"f32", "[8]"}, writes: []string{"/data/a"}},
			{op: "newtensor", reads: []string{"f32", "[8]"}, writes: []string{"/data/b"}},
			{op: "newtensor", reads: []string{"f32", "[8]"}, writes: []string{"/data/c"}},
			{op: "zeros", reads: nil, writes: []string{"/data/a"}},
			{op: "zeros", reads: nil, writes: []string{"/data/b"}},
			{op: "add", reads: []string{"/data/a", "/data/b"}, writes: []string{"/data/c"}},
			{op: "deltensor", reads: []string{"/data/a"}, writes: nil},
			{op: "deltensor", reads: []string{"/data/b"}, writes: nil},
		})
	})

}

// ── Call / Function Nesting ─────────────────────────────────

func TestParse_Call(t *testing.T) {
	t.Run("add_test", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/add_test.kv", []wantInst{
			{op: "add", reads: []string{"A", "B"}, writes: []string{"./C"}},
		})
	})
	t.Run("callee", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/callee.kv", []wantInst{
			{op: "+", reads: []string{"X", "Y"}, writes: []string{"./Z"}},
		})
	})
	t.Run("caller", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/caller.kv", []wantInst{
			{op: "callee", reads: []string{"A", "B"}, writes: []string{"./C"}},
		})
	})
	t.Run("middle", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/middle.kv", []wantInst{
			{op: "leaf", reads: []string{"X"}, writes: []string{"./tmp"}},
			{op: "+", reads: []string{"./tmp", "1"}, writes: []string{"./Y"}},
		})
	})
	t.Run("deep3", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/deep3.kv", []wantInst{
			{op: "middle", reads: []string{"X"}, writes: []string{"./Y"}},
		})
	})
	t.Run("diamond", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/diamond.kv", []wantInst{
			{op: "double", reads: []string{"A"}, writes: []string{"./d"}},
			{op: "triple", reads: []string{"A"}, writes: []string{"./t"}},
			{op: "+", reads: []string{"./d", "./t"}, writes: []string{"./R"}},
		})
	})
	t.Run("double", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/double.kv", []wantInst{
			{op: "*", reads: []string{"X", "2"}, writes: []string{"./Y"}},
		})
	})
	t.Run("triple", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/triple.kv", []wantInst{
			{op: "*", reads: []string{"X", "3"}, writes: []string{"./Y"}},
		})
	})
}

// ── Native: Arithmetic ──────────────────────────────────────

func TestParse_NativeArith(t *testing.T) {
	tests := []struct {
		name string
		file string
		op   string
	}{
		{"add", "../../example/kvlang/builtin/arith/add.kv", "+"},
		{"sub", "../../example/kvlang/builtin/arith/sub.kv", "-"},
		{"mul", "../../example/kvlang/builtin/arith/mul.kv", "*"},
		{"div", "../../example/kvlang/builtin/arith/div.kv", "/"},
		{"neg", "../../example/kvlang/builtin/arith/neg.kv", "neg"},
		{"abs", "../../example/kvlang/builtin/arith/abs.kv", "abs"},
		{"sign", "../../example/kvlang/builtin/arith/sign.kv", "sign"},
		{"pow", "../../example/kvlang/builtin/arith/pow.kv", "pow"},
		{"exp", "../../example/kvlang/builtin/arith/exp.kv", "exp"},
		{"log", "../../example/kvlang/builtin/arith/log.kv", "log"},
		{"sqrt", "../../example/kvlang/builtin/arith/sqrt.kv", "sqrt"},
		{"max", "../../example/kvlang/builtin/arith/max.kv", "max"},
		{"min", "../../example/kvlang/builtin/arith/min.kv", "min"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			fn, err := loadFirstFunc(tt.file)
			if err != nil {
				t.Fatal(err)
			}
			if len(fn.Body) == 0 {
				t.Fatal("empty body")
			}
			// 每个 buildin 示例现在都以 print 语句开头和结尾
			// 首行应为 print (输入打印)
			firstInst, err := parser.ParseLine(fn.Body[0].String())
			if err != nil {
				t.Fatal(err)
			}
			if firstInst.Opcode != "print" {
				t.Errorf("[%s] first opcode=%s, want print", tt.name, firstInst.Opcode)
			}
			// 核心计算在倒数第二行 (最后一行是 print("./C"))
			computeLine := fn.Body[len(fn.Body)-2].String()
			inst, err := parser.ParseLine(computeLine)
			if err != nil {
				t.Fatal(err)
			}
			if inst.Opcode != tt.op {
				t.Errorf("[%s] opcode=%s, want %s", tt.name, inst.Opcode, tt.op)
			}
		})
	}
}

// ── Native: Compare / Logic / Cast / Chain ──────────────────

func TestParse_NativeOther(t *testing.T) {
	t.Run("compare", func(t *testing.T) {
		t.Run("eq", func(t *testing.T) {
			checkKv(t, "../../example/kvlang/builtin/compare/eq.kv", []wantInst{
				{op: "print", reads: []string{"A =", "A"}, writes: nil},
				{op: "print", reads: []string{"B =", "B"}, writes: nil},
				{op: "==", reads: []string{"A", "B"}, writes: []string{"./C"}},
				{op: "print", reads: []string{"C =", "./C"}, writes: nil},
			})
		})
		t.Run("lt", func(t *testing.T) {
			checkKv(t, "../../example/kvlang/builtin/compare/lt.kv", []wantInst{
				{op: "print", reads: []string{"A =", "A"}, writes: nil},
				{op: "print", reads: []string{"B =", "B"}, writes: nil},
				{op: "<", reads: []string{"A", "B"}, writes: []string{"./C"}},
				{op: "print", reads: []string{"C =", "./C"}, writes: nil},
			})
		})
	})
	t.Run("logic", func(t *testing.T) {
		t.Run("and", func(t *testing.T) {
			checkKv(t, "../../example/kvlang/builtin/logic/and.kv", []wantInst{
				{op: "print", reads: []string{"A =", "A"}, writes: nil},
				{op: "print", reads: []string{"B =", "B"}, writes: nil},
				{op: "&&", reads: []string{"A", "B"}, writes: []string{"./C"}},
				{op: "print", reads: []string{"C =", "./C"}, writes: nil},
			})
		})
		t.Run("not", func(t *testing.T) {
			checkKv(t, "../../example/kvlang/builtin/logic/not.kv", []wantInst{
				{op: "print", reads: []string{"A =", "A"}, writes: nil},
				{op: "!", reads: []string{"A"}, writes: []string{"./C"}},
				{op: "print", reads: []string{"C =", "./C"}, writes: nil},
			})
		})
		t.Run("bool", func(t *testing.T) {
			checkKv(t, "../../example/kvlang/builtin/logic/bool.kv", []wantInst{
				{op: "print", reads: []string{"A =", "A"}, writes: nil},
				{op: "bool", reads: []string{"A"}, writes: []string{"./C"}},
				{op: "print", reads: []string{"C =", "./C"}, writes: nil},
			})
		})
	})
	t.Run("cast", func(t *testing.T) {
		t.Run("int", func(t *testing.T) {
			checkKv(t, "../../example/kvlang/builtin/cast/int.kv", []wantInst{
				{op: "print", reads: []string{"A =", "A"}, writes: nil},
				{op: "int", reads: []string{"A"}, writes: []string{"./C"}},
				{op: "print", reads: []string{"C =", "./C"}, writes: nil},
			})
		})
		t.Run("float", func(t *testing.T) {
			checkKv(t, "../../example/kvlang/builtin/cast/float.kv", []wantInst{
				{op: "print", reads: []string{"A =", "A"}, writes: nil},
				{op: "float", reads: []string{"A"}, writes: []string{"./C"}},
				{op: "print", reads: []string{"C =", "./C"}, writes: nil},
			})
		})
	})
	t.Run("chain", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/chain/chain.kv", []wantInst{
			{op: "print", reads: []string{"A =", "A"}, writes: nil},
			{op: "print", reads: []string{"B =", "B"}, writes: nil},
			{op: "print", reads: []string{"C =", "C"}, writes: nil},
			{op: "+", reads: []string{"A", "B"}, writes: []string{"./tmp"}},
			{op: "print", reads: []string{"tmp =", "./tmp"}, writes: nil},
			{op: "*", reads: []string{"./tmp", "C"}, writes: []string{"./D"}},
			{op: "print", reads: []string{"D =", "./D"}, writes: nil},
		})
	})
}

// ── New Examples: Multi-read/write, C-style (<-) ──────────

func TestParse_NewExamples(t *testing.T) {
	t.Run("double_op", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/arith/double_op.kv", []wantInst{
			{op: "print", reads: []string{"A =", "A"}, writes: nil},
			{op: "print", reads: []string{"B =", "B"}, writes: nil},
			{op: "+", reads: []string{"A", "B"}, writes: []string{"./S"}},
			{op: "print", reads: []string{"S =", "./S"}, writes: nil},
			{op: "-", reads: []string{"A", "B"}, writes: []string{"./D"}},
			{op: "print", reads: []string{"D =", "./D"}, writes: nil},
		})
	})
	t.Run("double_op_cstyle", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/arith/double_op_cstyle.kv", []wantInst{
			{op: "print", reads: []string{"A =", "A"}, writes: nil},
			{op: "print", reads: []string{"B =", "B"}, writes: nil},
			{op: "+", reads: []string{"A", "B"}, writes: []string{"./S"}},
			{op: "print", reads: []string{"S =", "./S"}, writes: nil},
			{op: "-", reads: []string{"A", "B"}, writes: []string{"./D"}},
			{op: "print", reads: []string{"D =", "./D"}, writes: nil},
		})
	})
	t.Run("three_add", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/arith/three_add.kv", []wantInst{
			{op: "print", reads: []string{"A =", "A"}, writes: nil},
			{op: "print", reads: []string{"B =", "B"}, writes: nil},
			{op: "+", reads: []string{"A", "B"}, writes: []string{"./t"}},
			{op: "print", reads: []string{"t =", "./t"}, writes: nil},
			{op: "print", reads: []string{"C =", "C"}, writes: nil},
			{op: "+", reads: []string{"./t", "C"}, writes: []string{"./R"}},
			{op: "print", reads: []string{"R =", "./R"}, writes: nil},
		})
	})
	t.Run("poly3", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/arith/poly3.kv", []wantInst{
			{op: "*", reads: []string{"A", "X"}, writes: []string{"./t1"}},
			{op: "print", reads: []string{"t1 =", "./t1"}, writes: nil},
			{op: "*", reads: []string{"./t1", "X"}, writes: []string{"./t2"}},
			{op: "print", reads: []string{"t2 =", "./t2"}, writes: nil},
			{op: "*", reads: []string{"B", "X"}, writes: []string{"./t3"}},
			{op: "print", reads: []string{"t3 =", "./t3"}, writes: nil},
			{op: "+", reads: []string{"./t2", "./t3"}, writes: []string{"./t4"}},
			{op: "print", reads: []string{"t4 =", "./t4"}, writes: nil},
			{op: "+", reads: []string{"./t4", "C"}, writes: []string{"./Y"}},
			{op: "print", reads: []string{"Y =", "./Y"}, writes: nil},
		})
	})
	t.Run("multi_io", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/multi_io.kv", []wantInst{
			{op: "+", reads: []string{"X", "Y"}, writes: []string{"./t"}},
			{op: "+", reads: []string{"./t", "Z"}, writes: []string{"./R"}},
		})
	})
	t.Run("multi_ret", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/call/multi_ret.kv", []wantInst{
			{op: "+", reads: []string{"A", "B"}, writes: []string{"./S"}},
			{op: "-", reads: []string{"A", "B"}, writes: []string{"./D"}},
			{op: "*", reads: []string{"A", "B"}, writes: []string{"./P"}},
		})
	})
	t.Run("add_sub", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/tensor/lifecycle/add_sub.kv", []wantInst{
			{op: "newtensor", reads: []string{"f32", "[64]"}, writes: []string{"/data/a"}},
			{op: "newtensor", reads: []string{"f32", "[64]"}, writes: []string{"/data/b"}},
			{op: "newtensor", reads: []string{"f32", "[64]"}, writes: []string{"/data/sum"}},
			{op: "newtensor", reads: []string{"f32", "[64]"}, writes: []string{"/data/diff"}},
			{op: "zeros", reads: nil, writes: []string{"/data/a"}},
			{op: "zeros", reads: nil, writes: []string{"/data/b"}},
			{op: "add", reads: []string{"/data/a", "/data/b"}, writes: []string{"./sum"}},
			{op: "sub", reads: []string{"/data/a", "/data/b"}, writes: []string{"./diff"}},
			{op: "deltensor", reads: []string{"/data/a"}, writes: nil},
			{op: "deltensor", reads: []string{"/data/b"}, writes: nil},
		})
	})
	t.Run("compute_cstyle", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/tensor/lifecycle/compute_cstyle.kv", []wantInst{
			{op: "newtensor", reads: []string{"f32", "[8]"}, writes: []string{"/data/a"}},
			{op: "newtensor", reads: []string{"f32", "[8]"}, writes: []string{"/data/b"}},
			{op: "newtensor", reads: []string{"f32", "[8]"}, writes: []string{"/data/c"}},
			{op: "zeros", reads: nil, writes: []string{"/data/a"}},
			{op: "zeros", reads: nil, writes: []string{"/data/b"}},
			{op: "add", reads: []string{"/data/a", "/data/b"}, writes: []string{"./c"}},
			{op: "deltensor", reads: []string{"/data/a"}, writes: nil},
			{op: "deltensor", reads: []string{"/data/b"}, writes: nil},
		})
	})
}

// ── Native: Print ────────────────────────────────────────────

func TestParse_Print(t *testing.T) {
	t.Run("print_int", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/print/print_int.kv", []wantInst{
			{op: "print", reads: []string{"X =", "X"}, writes: nil},
			{op: "+", reads: []string{"X", "0"}, writes: []string{"./R"}},
			{op: "print", reads: []string{"R =", "./R"}, writes: nil},
		})
	})
	t.Run("print_multi", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/print/print_multi.kv", []wantInst{
			{op: "print", reads: []string{"A =", "A"}, writes: nil},
			{op: "print", reads: []string{"B =", "B"}, writes: nil},
			{op: "print", reads: []string{"C =", "C"}, writes: nil},
			{op: "+", reads: []string{"A", "B"}, writes: []string{"./t"}},
			{op: "print", reads: []string{"t =", "./t"}, writes: nil},
			{op: "+", reads: []string{"./t", "C"}, writes: []string{"./R"}},
			{op: "print", reads: []string{"R =", "./R"}, writes: nil},
		})
	})
	t.Run("print_bool", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/print/print_bool.kv", []wantInst{
			{op: "bool", reads: []string{"A"}, writes: []string{"./C"}},
			{op: "print", reads: []string{"C =", "./C"}, writes: nil},
		})
	})
	t.Run("print_chain", func(t *testing.T) {
		checkKv(t, "../../example/kvlang/builtin/print/print_chain.kv", []wantInst{
			{op: "print", reads: []string{"A =", "A"}, writes: nil},
			{op: "+", reads: []string{"A", "B"}, writes: []string{"./tmp"}},
			{op: "print", reads: []string{"tmp =", "./tmp"}, writes: nil},
			{op: "print", reads: []string{"C =", "C"}, writes: nil},
			{op: "*", reads: []string{"./tmp", "C"}, writes: []string{"./D"}},
			{op: "print", reads: []string{"D =", "./D"}, writes: nil},
		})
	})
}
