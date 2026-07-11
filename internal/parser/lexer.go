// Tokenize: kvlang 源码行 → Token 流。
// 字符串字面量（单/双引号）的引号在此处去除，后续解析层直接处理裸值。
package parser

import (
	"fmt"
	"strings"
)

// Kind 标记 Token 类型。
type Kind int

const (
	Ident   Kind = iota // 标识符、符号算子、未引用数字/路径段
	Literal             // 字面量（引号已去除）
	Arrow               // -> 或 <-
	LParen              // (
	RParen              // )
	Comma               // ,
	LBrace              // {
	RBrace              // }
	Return              // return
	If                  // if
	Else                // else
	For                 // for
)

// String 返回 Kind 的可读名称。
func (k Kind) String() string {
	switch k {
	case Ident:
		return "IDENT"
	case Literal:
		return "LITERAL"
	case Arrow:
		return "ARROW"
	case LParen:
		return "LPAREN"
	case RParen:
		return "RPAREN"
	case Comma:
		return "COMMA"
	case LBrace:
		return "LBRACE"
	case RBrace:
		return "RBRACE"
	case Return:
		return "RETURN"
	case If:
		return "IF"
	case Else:
		return "ELSE"
	case For:
		return "FOR"
	default:
		return "UNKNOWN"
	}
}

// Token 表示一个词法单元。
type Token struct {
	Kind  Kind
	Value string
}

// String 返回 Token 的调试表示。
func (t Token) String() string { return fmt.Sprintf("%s(%q)", t.Kind, t.Value) }

// Tokenize 将一行 kvlang 代码分割为 Token 列表。
//
// 规则：
//   - 单/双引号字符串 → Literal（引号已去除）
//   - 路径 ./foo → Literal
//   - 数字 42, 3.14 → Literal
//   - <- / -> → Arrow
//   - 双字符算子 ==, !=, <=, >= 等 → Ident
//   - 单字符算子 +, -, *, /, %, !, <, > 等 → Ident
//   - 关键字 return/if/else/for → 对应 Kind
//   - 其余标识符（含点号，如 tensor.new） → Ident
func Tokenize(line string) []Token {
	line = strings.TrimSpace(line)
	if line == "" {
		return nil
	}

	var tokens []Token
	i := 0
	for i < len(line) {
		c := line[i]

		// 空白
		if c == ' ' || c == '\t' {
			i++
			continue
		}

		// 单行注释
		if c == '#' {
			break
		}

		// 单引号字符串 '...' → Literal（引号去除）
		if c == '\'' {
			end := strings.IndexByte(line[i+1:], '\'')
			if end >= 0 {
				val := line[i+1 : i+1+end]
				tokens = append(tokens, Token{Kind: Literal, Value: val})
				i += end + 2
			} else {
				tokens = append(tokens, Token{Kind: Literal, Value: line[i+1:]})
				i = len(line)
			}
			continue
		}

		// 双引号字符串 "..." → Literal（引号去除）
		if c == '"' {
			end := strings.IndexByte(line[i+1:], '"')
			if end >= 0 {
				val := line[i+1 : i+1+end]
				tokens = append(tokens, Token{Kind: Literal, Value: val})
				i += end + 2
			} else {
				tokens = append(tokens, Token{Kind: Literal, Value: line[i+1:]})
				i = len(line)
			}
			continue
		}

		// 左箭头 <- （在双字符算子之前匹配，避免 <= 误判）
		if c == '<' && i+1 < len(line) && line[i+1] == '-' {
			tokens = append(tokens, Token{Kind: Arrow, Value: "<-"})
			i += 2
			continue
		}

		// 右箭头 ->
		if c == '-' && i+1 < len(line) && line[i+1] == '>' {
			tokens = append(tokens, Token{Kind: Arrow, Value: "->"})
			i += 2
			continue
		}

		// 双字符算子（在单字符之前匹配）
		if i+1 < len(line) {
			two := line[i : i+2]
			switch two {
			case "==", "!=", "<=", ">=", "&&", "||", "<<", ">>":
				tokens = append(tokens, Token{Kind: Ident, Value: two})
				i += 2
				continue
			}
		}

		// 单字符分隔符/括号
		switch c {
		case '(':
			tokens = append(tokens, Token{Kind: LParen, Value: "("})
			i++
			continue
		case ')':
			tokens = append(tokens, Token{Kind: RParen, Value: ")"})
			i++
			continue
		case ',':
			tokens = append(tokens, Token{Kind: Comma, Value: ","})
			i++
			continue
		case '{':
			tokens = append(tokens, Token{Kind: LBrace, Value: "{"})
			i++
			continue
		case '}':
			tokens = append(tokens, Token{Kind: RBrace, Value: "}"})
			i++
			continue
		}

		// 单字符符号算子（不含 - 和 /，单独处理）
		switch c {
		case '+', '*', '%', '!', '<', '>', '&', '|', '^':
			tokens = append(tokens, Token{Kind: Ident, Value: string(c)})
			i++
			continue
		}

		// '-' 单独处理（已排除 -> 的情况）
		if c == '-' {
			tokens = append(tokens, Token{Kind: Ident, Value: "-"})
			i++
			continue
		}

		// 数字字面量（不含负号，负号由 '-' Ident 表示）
		if c >= '0' && c <= '9' {
			start := i
			for i < len(line) && (line[i] >= '0' && line[i] <= '9' || line[i] == '.' || line[i] == 'e' || line[i] == 'E') {
				i++
			}
			tokens = append(tokens, Token{Kind: Literal, Value: line[start:i]})
			continue
		}

		// 路径字面量 ./foo（必须先于关键字读取）
		if c == '.' && i+1 < len(line) && line[i+1] == '/' {
			start := i
			i += 2
			for i < len(line) && !isTokenDelim(line[i]) {
				i++
			}
			tokens = append(tokens, Token{Kind: Literal, Value: line[start:i]})
			continue
		}

		// '/' — 除法算子或绝对路径（在源码中路径总是单引号引用，此处按算子处理）
		if c == '/' {
			tokens = append(tokens, Token{Kind: Ident, Value: "/"})
			i++
			continue
		}

		// 关键字 / 标识符（含点号：tensor.new 等）
		start := i
		for i < len(line) && !isTokenDelim(line[i]) {
			i++
		}
		if i == start {
			// 当前字符是分隔符但没有匹配的具体处理（如裸 =）→ 跳过，防止无限循环
			i++
			continue
		}
		word := line[start:i]
		switch word {
		case "return":
			tokens = append(tokens, Token{Kind: Return, Value: word})
		case "if":
			tokens = append(tokens, Token{Kind: If, Value: word})
		case "else":
			tokens = append(tokens, Token{Kind: Else, Value: word})
		case "for":
			tokens = append(tokens, Token{Kind: For, Value: word})
		default:
			tokens = append(tokens, Token{Kind: Ident, Value: word})
		}
	}
	return tokens
}

// isTokenDelim 判断字节是否为 Token 边界（不含 '.' 和 '/'，允许 tensor.new 为整体标识符）。
func isTokenDelim(c byte) bool {
	switch c {
	case ' ', '\t', ',', ')', '(', '{', '}', '+', '-', '*', '%', '!', '=', '<', '>', '&', '|', '^':
		return true
	}
	return false
}
