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

		var rawBody []string
		bodyEnd := len(lines)
		if strings.HasSuffix(defLine, "{") {
			depth := 1
			for j := i + 1; j < len(lines); j++ {
				depth += strings.Count(lines[j], "{") - strings.Count(lines[j], "}")
				if depth == 0 {
					bodyEnd = j
					break
				}
				rawBody = append(rawBody, lines[j])
			}
			if bodyEnd == len(lines) {
				return nil, fmt.Errorf("unclosed brace in %s", name)
			}
			i = bodyEnd + 1
		} else {
			for j := i + 1; j < len(lines); j++ {
				if strings.HasPrefix(lines[j], "def ") || looksLikeCall(lines[j]) {
					bodyEnd = j
					break
				}
				rawBody = append(rawBody, lines[j])
			}
			i = bodyEnd
		}
		if len(rawBody) == 0 {
			return nil, fmt.Errorf("empty body for %s", name)
		}
		df.Funcs = append(df.Funcs, ast.Func{
			Name:      name,
			Signature: strings.TrimSuffix(defLine, " {"),
			Body:      parseBody(rawBody),
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
				if strings.HasPrefix(line, bl.FirstLine()) {
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
	sig = strings.TrimSpace(strings.TrimSuffix(strings.TrimSpace(sig), " {"))
	if idx := strings.Index(sig, "->"); idx >= 0 {
		sig = strings.TrimSpace(sig[:idx])
	}
	if idx := strings.Index(sig, "("); idx >= 0 {
		return strings.TrimSpace(sig[:idx])
	}
	return sig
}

// looksLikeCall 判断行是否为顶层调用（包含 isCallExpr 逻辑）。
func looksLikeCall(line string) bool {
	if strings.HasPrefix(line, "def ") || line == "}" || line == "{" {
		return false
	}
	if strings.Contains(line, "->") {
		return true
	}
	open := strings.Index(line, "(")
	return open > 0 &&
		!strings.HasPrefix(line, "if ") &&
		!strings.HasPrefix(line, "for ") &&
		!strings.HasPrefix(line, "while ")
}

func parseTopLevelCall(line string) (ast.TopLevelCall, bool) {
	arrowIdx := strings.Index(line, "->")
	left, right := strings.TrimSpace(line), ""
	if arrowIdx >= 0 {
		left = strings.TrimSpace(line[:arrowIdx])
		right = strings.TrimSpace(line[arrowIdx+2:])
	}
	open := strings.Index(left, "(")
	close := strings.LastIndex(left, ")")
	if open < 0 || close <= open {
		return ast.TopLevelCall{}, false
	}
	funcName := strings.TrimSpace(left[:open])
	if funcName == "" {
		return ast.TopLevelCall{}, false
	}
	var args []string
	if s := strings.TrimSpace(left[open+1 : close]); s != "" {
		for _, a := range strings.Split(s, ",") {
			if a = strings.TrimSpace(a); a != "" {
				args = append(args, a)
			}
		}
	}
	var outputs []string
	if right = strings.Trim(right, "()"); right != "" {
		for _, o := range strings.Split(right, ",") {
			if o = strings.Trim(strings.TrimSpace(o), `"'`); o != "" {
				outputs = append(outputs, o)
			}
		}
	}
	return ast.TopLevelCall{FuncName: funcName, Args: args, Outputs: outputs}, true
}

// parseBody 将原始行列表解析为 AST Stmt 节点。
// 以 Tokenize 首 Token 的 Value 进行分发，消除 strings.HasPrefix 双写。
func parseBody(lines []string) []ast.Stmt {
	var stmts []ast.Stmt
	i := 0
	for i < len(lines) {
		line := lines[i]
		if line == "" || line == "}" {
			i++
			continue
		}
		toks := Tokenize(line)
		if len(toks) == 0 {
			i++
			continue
		}
		switch toks[0].Value {
		case "while":
			st, next := parseWhileStmt(lines, i)
			stmts = append(stmts, st)
			i = next
		case "for":
			st, next := parseForStmt(lines, i)
			stmts = append(stmts, st)
			i = next
		case "if":
			st, next := parseIfStmt(lines, i)
			stmts = append(stmts, st)
			i = next
		case "break":
			stmts = append(stmts, &ast.BreakStmt{})
			i++
		case "continue":
			stmts = append(stmts, &ast.ContinueStmt{})
			i++
		default:
			if isBlockStart(line) {
				st, next := parseBlock(lines, i)
				stmts = append(stmts, st)
				i = next
			} else {
				if inst, _ := ParseLine(line); inst != nil {
					stmts = append(stmts, inst)
				}
				i++
			}
		}
	}
	return stmts
}

// parseBracedBody 解析花括号块体（调用前开括号已消费，depth 从 1 开始）。
func parseBracedBody(lines []string, start int) ([]ast.Stmt, int) {
	var body []ast.Stmt
	depth := 1
	i := start
	for i < len(lines) && depth > 0 {
		depth += strings.Count(lines[i], "{") - strings.Count(lines[i], "}")
		if depth == 0 {
			i++
			break
		}
		body = append(body, parseBody([]string{lines[i]})...)
		i++
	}
	return body, i
}

// parseIfStmt 解析 if/else 块。
func parseIfStmt(lines []string, start int) (*ast.IfStmt, int) {
	line := lines[start]
	s := &ast.IfStmt{Cond: strings.TrimSpace(line[strings.Index(line, "(")+1 : strings.LastIndex(line, ")")])}

	// then 分支：遇到 "} else {" 时手动调整 depth
	depth := 1
	i := start + 1
	for i < len(lines) && depth > 0 {
		depth += strings.Count(lines[i], "{") - strings.Count(lines[i], "}")
		if strings.HasPrefix(lines[i], "} else {") {
			depth-- // "} else {" 净括号为 0，需手动减去 }
			if depth == 0 {
				i++
				break
			}
		}
		if depth == 0 {
			break
		}
		s.Then = append(s.Then, parseBody([]string{lines[i]})...)
		i++
	}
	// else 分支
	if i < len(lines) {
		s.Else, i = parseBracedBody(lines, i)
	}
	return s, i
}

// parseForStmt 解析 for 循环: for (var in iter_path) { body }
func parseForStmt(lines []string, start int) (*ast.ForStmt, int) {
	line := lines[start]
	inner := line[strings.Index(line, "(")+1 : strings.LastIndex(line, ")")]
	parts := strings.SplitN(inner, " in ", 2)
	varName := strings.TrimSpace(parts[0])
	if colon := strings.Index(varName, ":"); colon >= 0 {
		varName = varName[:colon]
	}
	s := &ast.ForStmt{Var: varName, Iter: strings.TrimSpace(parts[1])}
	body, next := parseBracedBody(lines, start+1)
	s.Body = body
	return s, next
}

// parseWhileStmt 解析 while 循环: while (cond) { body }
func parseWhileStmt(lines []string, start int) (*ast.WhileStmt, int) {
	line := lines[start]
	inner := line[strings.Index(line, "(")+1 : strings.LastIndex(line, ")")]
	s := &ast.WhileStmt{Cond: strings.TrimSpace(inner)}
	body, next := parseBracedBody(lines, start+1)
	s.Body = body
	return s, next
}

// isBlockStart 判断行是否为 block label 定义（如 entry: {）。
func isBlockStart(line string) bool {
	colonIdx := strings.Index(line, ":")
	if colonIdx < 0 {
		return false
	}
	prefix := strings.TrimSpace(line[:colonIdx])
	return prefix != "" &&
		!strings.Contains(prefix, "(") &&
		!strings.HasPrefix(prefix, "/") &&
		!strings.HasPrefix(prefix, "./") &&
		!strings.Contains(prefix, " ")
}

// parseBlock 解析基本块: label: { body } 或 label:\n{ body }
func parseBlock(lines []string, start int) (*ast.BlockStmt, int) {
	line := lines[start]
	colonIdx := strings.Index(line, ":")
	s := &ast.BlockStmt{Label: strings.TrimSpace(line[:colonIdx])}
	rest := strings.TrimSpace(line[colonIdx+1:])

	bodyStart := start + 1
	if rest == "" && bodyStart < len(lines) && strings.TrimSpace(lines[bodyStart]) == "{" {
		bodyStart++ // 花括号在下一行，跳过 "{"
	} else if !strings.HasPrefix(rest, "{") {
		return s, start + 1
	}
	body, next := parseBracedBody(lines, bodyStart)
	s.Body = body
	return s, next
}
