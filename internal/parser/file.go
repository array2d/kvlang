package parser

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"strings"

	"kvlang/internal/ast"
)

// ParseFile 打开并解析 .kv 源文件。
func ParseFile(path string) (*ast.File, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", path, err)
	}
	defer f.Close()
	return ParseCode(f)
}

// ParseCode 从 io.Reader 解析 kvlang 代码，返回函数定义和顶层调用。
func ParseCode(r io.Reader) (*ast.File, error) {
	var lines []string
	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		lines = append(lines, line)
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}
	if len(lines) == 0 {
		return nil, fmt.Errorf("empty input")
	}

	return parseLines(lines)
}

func parseLines(lines []string) (*ast.File, error) {
	df := &ast.File{}
	i := 0
	for i < len(lines) {
		if !strings.HasPrefix(lines[i], "def ") {
			i++
			continue
		}
		defLine := lines[i]
		name := extractFuncName(defLine)
		if name == "" {
			return nil, fmt.Errorf("cannot extract name from: %s", defLine)
		}

		var body []string
		bodyEnd := len(lines)
		if strings.HasSuffix(defLine, "{") {
			for j := i + 1; j < len(lines); j++ {
				if lines[j] == "}" {
					bodyEnd = j
					break
				}
				body = append(body, lines[j])
			}
			if bodyEnd == len(lines) {
				return nil, fmt.Errorf("unclosed brace in %s", name)
			}
			i = bodyEnd + 1
		} else {
			for j := i + 1; j < len(lines); j++ {
				if strings.HasPrefix(lines[j], "def ") {
					bodyEnd = j
					break
				}
				if looksLikeCall(lines[j]) {
					bodyEnd = j
					break
				}
				body = append(body, lines[j])
			}
			i = bodyEnd
		}
		if len(body) == 0 {
			return nil, fmt.Errorf("empty body for %s", name)
		}
		df.Funcs = append(df.Funcs, ast.Func{
			Name:      name,
			Signature: strings.TrimSuffix(defLine, " {"),
			Body:      body,
		})
	}
	if len(df.Funcs) == 0 {
		return nil, fmt.Errorf("no 'def' found")
	}

	// 顶层调用
	for _, line := range lines {
		if strings.HasPrefix(line, "def ") || line == "}" {
			continue
		}
		inBody := false
		for _, fn := range df.Funcs {
			for _, bl := range fn.Body {
				if bl == line {
					inBody = true
					break
				}
			}
			if inBody {
				break
			}
		}
		if inBody {
			continue
		}
		if tc, ok := parseTopLevelCall(line); ok {
			df.TopLevelCalls = append(df.TopLevelCalls, tc)
			df.PreambleLines = append(df.PreambleLines, line)
		}
	}
	return df, nil
}

// ── file helpers ──

func extractFuncName(sig string) string {
	sig = strings.TrimSpace(sig)
	if strings.HasPrefix(sig, "def ") {
		sig = strings.TrimSpace(sig[4:])
	}
	if len(sig) >= 2 && sig[0] == '(' && sig[len(sig)-1] == ')' {
		sig = sig[1 : len(sig)-1]
	}
	sig = strings.TrimSuffix(sig, " {")
	sig = strings.TrimSpace(sig)
	left := sig
	if idx := strings.Index(sig, "->"); idx >= 0 {
		left = strings.TrimSpace(sig[:idx])
	}
	if idx := strings.Index(left, "("); idx >= 0 {
		return strings.TrimSpace(left[:idx])
	}
	return left
}

func looksLikeCall(line string) bool {
	if strings.HasPrefix(line, "def ") || line == "}" || line == "{" {
		return false
	}
	return strings.Contains(line, "->")
}

func parseTopLevelCall(line string) (ast.TopLevelCall, bool) {
	arrowIdx := strings.Index(line, "->")
	if arrowIdx < 0 {
		return ast.TopLevelCall{}, false
	}
	left := strings.TrimSpace(line[:arrowIdx])
	right := strings.TrimSpace(line[arrowIdx+2:])
	open := strings.Index(left, "(")
	if open < 0 {
		return ast.TopLevelCall{}, false
	}
	close := strings.LastIndex(left, ")")
	if close < 0 || close <= open {
		return ast.TopLevelCall{}, false
	}
	funcName := strings.TrimSpace(left[:open])
	if funcName == "" {
		return ast.TopLevelCall{}, false
	}
	var args []string
	s := strings.TrimSpace(left[open+1 : close])
	if s != "" {
		for _, a := range strings.Split(s, ",") {
			a = strings.TrimSpace(a)
			if a != "" {
				args = append(args, a)
			}
		}
	}
	var outputs []string
	right = strings.Trim(right, "()")
	if right != "" {
		for _, o := range strings.Split(right, ",") {
			o = strings.TrimSpace(o)
			o = strings.Trim(o, `"'`)
			if o != "" {
				outputs = append(outputs, o)
			}
		}
	}
	return ast.TopLevelCall{FuncName: funcName, Args: args, Outputs: outputs}, true
}
