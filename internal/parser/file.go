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
				if strings.HasPrefix(lines[j], "def ") {
					bodyEnd = j
					break
				}
				if looksLikeCall(lines[j]) {
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
		body := parseBody(rawBody)
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
	return strings.Contains(line, "->") || isCallExpr(line)
}

func parseTopLevelCall(line string) (ast.TopLevelCall, bool) {
	arrowIdx := strings.Index(line, "->")
	left := strings.TrimSpace(line)
	right := ""
	if arrowIdx >= 0 {
		left = strings.TrimSpace(line[:arrowIdx])
		right = strings.TrimSpace(line[arrowIdx+2:])
	}
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

// parseBody 将原始行列表解析为 AST Stmt 节点。
func parseBody(lines []string) []ast.Stmt {
	var stmts []ast.Stmt
	i := 0
	for i < len(lines) {
		line := lines[i]
		if strings.HasPrefix(line, "while (") || strings.HasPrefix(line, "while(") {
			whileStmt, next := parseWhileStmt(lines, i)
			stmts = append(stmts, whileStmt)
			i = next
		} else if strings.HasPrefix(line, "break") {
			stmts = append(stmts, &ast.BreakStmt{})
			i++
		} else if strings.HasPrefix(line, "continue") {
			stmts = append(stmts, &ast.ContinueStmt{})
			i++
		} else if strings.HasPrefix(line, "for (") || strings.HasPrefix(line, "for(") {
			forStmt, next := parseForStmt(lines, i)
			stmts = append(stmts, forStmt)
			i = next
		} else if strings.HasPrefix(line, "if (") || strings.HasPrefix(line, "if(") {
			ifStmt, next := parseIfStmt(lines, i)
			stmts = append(stmts, ifStmt)
			i = next
		} else if isBlockStart(line) {
			block, next := parseBlock(lines, i)
			stmts = append(stmts, block)
			i = next
		} else {
			if line == "" || line == "}" {
				i++
				continue
			}
			inst, _ := ParseLine(line)
			if inst != nil {
				stmts = append(stmts, inst)
			}
			i++
		}
	}
	return stmts
}

// parseIfStmt 解析 if/else 块。
func parseIfStmt(lines []string, start int) (*ast.IfStmt, int) {
	line := lines[start]
	// Extract condition: if (cond) { or if(cond){
	condStart := strings.Index(line, "(")
	condEnd := strings.LastIndex(line, ")")
	cond := strings.TrimSpace(line[condStart+1 : condEnd])

	// Parse then body
	s := &ast.IfStmt{Cond: cond}
	depth := 1
	i := start + 1
	for i < len(lines) && depth > 0 {
		depth += strings.Count(lines[i], "{") - strings.Count(lines[i], "}")
		if strings.HasPrefix(lines[i], "} else {") || lines[i] == "} else {" {
			depth-- // close of `}`
			if depth == 0 {
				i++ // move past `} else {`
				break
			}
		}
		if depth == 0 {
			break
		}
		s.Then = append(s.Then, parseBody([]string{lines[i]})...)
		i++
	}

	// Parse else body (no `else {` prefix check — we're already inside it)
	if i < len(lines) {
		depth = 1
		for i < len(lines) && depth > 0 {
			depth += strings.Count(lines[i], "{") - strings.Count(lines[i], "}")
			if depth == 0 {
				i++
				break
			}
			s.Else = append(s.Else, parseBody([]string{lines[i]})...)
			i++
		}
	}
	return s, i
}

// parseForStmt 解析 for 循环: for (var in iter_path) { body }
// iter_path 是 kvspace 路径，如 './data', '/tensor/x'
func parseForStmt(lines []string, start int) (*ast.ForStmt, int) {
	line := lines[start]
	// Extract: for (var in iter_path) {
	inner := line[strings.Index(line, "(")+1 : strings.LastIndex(line, ")")]
	parts := strings.SplitN(inner, " in ", 2)
	varName := strings.TrimSpace(parts[0])
	if colon := strings.Index(varName, ":"); colon >= 0 {
		varName = varName[:colon]
	}
	iterPath := strings.TrimSpace(parts[1])

	s := &ast.ForStmt{Var: varName, Iter: iterPath}
	depth := 1
	i := start + 1
	for i < len(lines) && depth > 0 {
		depth += strings.Count(lines[i], "{") - strings.Count(lines[i], "}")
		if depth == 0 {
			i++
			break
		}
		s.Body = append(s.Body, parseBody([]string{lines[i]})...)
		i++
	}
	return s, i
}

// parseWhileStmt 解析 while 循环块: while (cond) { body }
func parseWhileStmt(lines []string, start int) (*ast.WhileStmt, int) {
	line := lines[start]
	inner := line[strings.Index(line, "(")+1 : strings.LastIndex(line, ")")]
	cond := strings.TrimSpace(inner)

	s := &ast.WhileStmt{Cond: cond}
	depth := 1
	i := start + 1
	for i < len(lines) && depth > 0 {
		depth += strings.Count(lines[i], "{") - strings.Count(lines[i], "}")
		if depth == 0 { i++; break }
		s.Body = append(s.Body, parseBody([]string{lines[i]})...)
		i++
	}
	return s, i
}

// isBlockStart 判断行是否为 block label 定义: "ident:" 或 "ident: {"
// 冒号右侧的 `:` 是 label 声明的标志（Assembly/MLIR 惯例），
// 定义处带 : ，引用处不带（如 br(cond, entry, then)）。
func isBlockStart(line string) bool {
	// 必须有 `:` 作为 label 声明标志
	colonIdx := strings.Index(line, ":")
	if colonIdx < 0 {
		return false
	}
	prefix := strings.TrimSpace(line[:colonIdx])
	if prefix == "" {
		return false
	}
	// 排除函数调用 ident(...   ——  冒号前的部分含 ( 说明是调用
	if strings.Contains(prefix, "(") {
		return false
	}
	// 排除 key 路径 /path:xxx  —— 冒号前的部分是路径
	if strings.HasPrefix(prefix, "/") || strings.HasPrefix(prefix, "./") {
		return false
	}
	// 排除 type 标注 A:int  —— 冒号在类型标注中
	if strings.Contains(prefix, " ") {
		return false
	}
	return true
}

// parseBlock 解析基本块: label: { body }
func parseBlock(lines []string, start int) (*ast.BlockStmt, int) {
	line := lines[start]
	colonIdx := strings.Index(line, ":")
	label := strings.TrimSpace(line[:colonIdx])

	// 寻找 `{` 或下一行开始 body
	rest := strings.TrimSpace(line[colonIdx+1:])
	s := &ast.BlockStmt{Label: label}

	// label: { stmts }  — 花括号在同一行
	if strings.HasPrefix(rest, "{") {
		// body 从下一行开始
		depth := 1
		i := start + 1
		for i < len(lines) && depth > 0 {
			depth += strings.Count(lines[i], "{") - strings.Count(lines[i], "}")
			if depth == 0 {
				i++
				break
			}
			s.Body = append(s.Body, parseBody([]string{lines[i]})...)
			i++
		}
		return s, i
	}

	// label:  — 花括号在下一行
	if rest == "" {
		i := start + 1
		if i < len(lines) && strings.TrimSpace(lines[i]) == "{" {
			depth := 1
			i++ // skip `{`
			for i < len(lines) && depth > 0 {
				depth += strings.Count(lines[i], "{") - strings.Count(lines[i], "}")
				if depth == 0 {
					i++
					break
				}
				s.Body = append(s.Body, parseBody([]string{lines[i]})...)
				i++
			}
			return s, i
		}
	}

	// 无法解析的格式
	return s, start + 1
}

func isCallExpr(line string) bool {
	open := strings.Index(line, "(")
	return open > 0 && !strings.HasPrefix(line, "if ") && !strings.HasPrefix(line, "for ") && !strings.HasPrefix(line, "while ")
}
