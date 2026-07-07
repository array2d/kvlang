package parser

import (
	"fmt"
	"strings"

	"kvlang/internal/ast"
)

// ParseLine 解析单条 kvlang 指令字符串。
//
// 支持三种赋值风格:
//
//	前缀 (命名函数):  add(A, B) -> ./C
//	中缀 (符号算子):  A + B -> ./C, !A -> ./C
//	C风格 (左箭头):   ./C <- A + B, ./C <- add(A, B)
func ParseLine(line string) (*ast.Instruction, error) {
	line = strings.TrimSpace(line)
	if line == "" {
		return nil, fmt.Errorf("empty kvlang line")
	}

	inst := &ast.Instruction{}

	// 1. 分离输出
	var expr string
	if larrow := findArrow(line, "<-"); larrow >= 0 {
		writesStr := strings.TrimSpace(line[:larrow])
		expr = strings.TrimSpace(line[larrow+2:])
		if strings.HasPrefix(writesStr, "(") && strings.HasSuffix(writesStr, ")") {
			writesStr = writesStr[1 : len(writesStr)-1]
		}
		if err := validateKeyRefs(writesStr, "write", line); err != nil {
			return nil, err
		}
		inst.Writes = parseParamList(writesStr)
	} else if arrow := strings.Index(line, "->"); arrow >= 0 {
		expr = strings.TrimSpace(line[:arrow])
		writesStr := strings.TrimSpace(line[arrow+2:])
		if strings.HasPrefix(writesStr, "(") && strings.HasSuffix(writesStr, ")") {
			writesStr = writesStr[1 : len(writesStr)-1]
		}
		if err := validateKeyRefs(writesStr, "write", line); err != nil {
			return nil, err
		}
		inst.Writes = parseParamList(writesStr)
	} else {
		expr = line
	}

	// 2. 中缀解析
	if op, left, right, ok := parseInfix(expr); ok {
		inst.Opcode = op
		if left != "" {
			if err := validateRef(left, line); err != nil {
				return nil, err
			}
			inst.Reads = append(inst.Reads, stripQuotes(left))
		}
		if right != "" {
			if err := validateRef(right, line); err != nil {
				return nil, err
			}
			inst.Reads = append(inst.Reads, stripQuotes(right))
		}
		return inst, nil
	}

	// 3. 前缀解析: add(A, B)
	if idx := strings.Index(expr, "("); idx >= 0 {
		inst.Opcode = strings.TrimSpace(expr[:idx])
		rest := expr[idx+1:]

		parenDepth := 1
		closeIdx := -1
		for i, c := range rest {
			if c == '(' {
				parenDepth++
			} else if c == ')' {
				parenDepth--
				if parenDepth == 0 {
					closeIdx = i
					break
				}
			}
		}
		if closeIdx < 0 {
			return nil, fmt.Errorf("unmatched paren in: %s", line)
		}
		readsStr := rest[:closeIdx]
		if err := validateKeyRefs(readsStr, "read", line); err != nil {
			return nil, err
		}
		inst.Reads = parseParamList(readsStr)
	}

	return inst, nil
}

// ── line helpers ──

func findArrow(s, arrow string) int {
	for i := 0; i < len(s)-1; i++ {
		if s[i] == arrow[0] && s[i+1] == arrow[1] {
			return i
		}
	}
	return -1
}

func parseInfix(expr string) (op, left, right string, ok bool) {
	expr = strings.TrimSpace(expr)
	if expr == "" {
		return
	}
	if strings.IndexByte(expr, '(') >= 0 {
		return // 含括号 → 前缀格式
	}
	multiOps := []string{"==", "!=", "<=", ">=", "&&", "||", "<<", ">>"}
	for _, o := range multiOps {
		if idx := strings.Index(expr, o); idx > 0 {
			return o, strings.TrimSpace(expr[:idx]), strings.TrimSpace(expr[idx+len(o):]), true
		}
	}
	singleOps := []string{"+", "*", "/", "%", "<", ">", "&", "|", "^"}
	for _, o := range singleOps {
		if idx := strings.Index(expr, o); idx > 0 {
			return o, strings.TrimSpace(expr[:idx]), strings.TrimSpace(expr[idx+1:]), true
		}
	}
	if idx := strings.Index(expr, "-"); idx > 0 {
		return "-", strings.TrimSpace(expr[:idx]), strings.TrimSpace(expr[idx+1:]), true
	}
	if len(expr) > 0 {
		if expr[0] == '!' {
			return "!", strings.TrimSpace(expr[1:]), "", true
		}
		if expr[0] == '-' {
			return "-", strings.TrimSpace(expr[1:]), "", true
		}
	}
	return
}

func parseParamList(s string) []string {
	s = strings.TrimSpace(s)
	if s == "" {
		return nil
	}
	var params []string
	depth := 0
	inQuote := byte(0)
	start := 0
	for i := 0; i < len(s); i++ {
		if s[i] == '"' || s[i] == '\'' {
			if inQuote == 0 {
				inQuote = s[i]
			} else if inQuote == s[i] {
				inQuote = 0
			}
			continue
		}
		if inQuote != 0 {
			continue
		}
		switch s[i] {
		case '[', '(', '{':
			depth++
		case ']', ')', '}':
			if depth > 0 {
				depth--
			}
		case ',':
			if depth == 0 {
				p := strings.TrimSpace(s[start:i])
				if p != "" {
					params = append(params, stripQuotes(p))
				}
				start = i + 1
			}
		}
	}
	if start < len(s) {
		p := strings.TrimSpace(s[start:])
		if p != "" {
			params = append(params, stripQuotes(p))
		}
	}
	return params
}

func parseParamListRaw(s string) []string {
	s = strings.TrimSpace(s)
	if s == "" {
		return nil
	}
	var params []string
	depth := 0
	inQuote := byte(0)
	start := 0
	for i := 0; i < len(s); i++ {
		if s[i] == '"' || s[i] == '\'' {
			if inQuote == 0 {
				inQuote = s[i]
			} else if inQuote == s[i] {
				inQuote = 0
			}
			continue
		}
		if inQuote != 0 {
			continue
		}
		switch s[i] {
		case '[', '(', '{':
			depth++
		case ']', ')', '}':
			if depth > 0 {
				depth--
			}
		case ',':
			if depth == 0 {
				p := strings.TrimSpace(s[start:i])
				if p != "" {
					params = append(params, p)
				}
				start = i + 1
			}
		}
	}
	if start < len(s) {
		p := strings.TrimSpace(s[start:])
		if p != "" {
			params = append(params, p)
		}
	}
	return params
}

func stripQuotes(s string) string {
	if len(s) >= 2 {
		if (s[0] == '"' && s[len(s)-1] == '"') || (s[0] == '\'' && s[len(s)-1] == '\'') {
			return s[1 : len(s)-1]
		}
	}
	return s
}
