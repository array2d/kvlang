package parsertest

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"kvlang/internal/ast"
	"kvlang/internal/lower"
	"kvlang/internal/parser"
)

func TestExampleFiles(t *testing.T) {
	exampleDir := os.Getenv("KV_EXAMPLES")
	if exampleDir == "" {
		cwd, _ := os.Getwd()
		for _, rel := range []string{"../../../../../example/kvlang", "../../../../example/kvlang", "../../../example/kvlang"} {
			candidate := filepath.Join(cwd, rel)
			if info, err := os.Stat(candidate); err == nil && info.IsDir() {
				exampleDir = candidate
				break
			}
		}
	}
	if exampleDir == "" {
		t.Skip("example dir not found")
	}

	var files []string
	filepath.Walk(exampleDir, func(p string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && strings.HasSuffix(p, ".kv") {
			files = append(files, p)
		}
		return nil
	})
	if len(files) == 0 {
		t.Skipf("no .kv files found in %s", exampleDir)
	}
	t.Logf("Testing %d .kv files from %s", len(files), exampleDir)

	for _, f := range files {
		f := f
		t.Run(filepath.Base(f), func(t *testing.T) {
			af, diags, err := parser.ParseFile(f)
			if err != nil {
				if strings.Contains(err.Error(), "no 'def' found") {
					t.Skipf("index-only file (no defs): %s", filepath.Base(f))
				}
				t.Fatalf("parse %s: %v", filepath.Base(f), err)
			}
			for _, d := range diags {
				t.Logf("diag: %s", d)
			}
			var sb strings.Builder
			ast.Format(&sb, af)
			for i := range af.Funcs {
				lower.Func(&af.Funcs[i])
			}
			t.Logf("OK: %d funcs, %d top-level", len(af.Funcs), len(af.TopLevelCalls))
			fmt.Fprintf(os.Stderr, "OK %s\n", filepath.Base(f))
		})
	}
}
