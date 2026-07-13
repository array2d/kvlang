// scanner.go: kvlang 词法分析。
// Scan(src string) → []Token  支持整文件多行扫描，末尾附 EOF 哨兵。
// Tokenize(line string)       向后兼容单行接口，过滤 Newline/EOF。
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
	Colon               // :
	Return              // return
	If                  // if
	Else                // else
	For                 // for
	While               // while
	Newline             // 换行（语句分隔符，连续换行折叠为一个）
	EOF                 // 文件结束哨兵
)

// String 返回 Kind 的可读名称。
func (k Kind) String() string {
	names := [...]string{
		"IDENT", "LITERAL", "ARROW",
		"LPAREN", "RPAREN", "COMMA", "LBRACE", "RBRACE", "COLON",
		"RETURN", "IF", "ELSE", "FOR", "WHILE", "NEWLINE", "EOF",
	}
	if int(k) < len(names) {
		return names[k]
	}
	return "UNKNOWN"
}

// Token 表示一个词法单元。
type Token struct {
	Kind  Kind
	Value string
}

// String 返回 Token 的调试表示。
func (t Token) String() string { return fmt.Sprintf("%s(%q)", t.Kind, t.Value) }

// singleCharToken 将单字符标点映射到对应 Kind。
var singleCharToken = map[byte]Kind{
	'(': LParen, ')': RParen, ',': Comma,
	'{': LBrace, '}': RBrace, ':': Colon,
}

// scanQuoted 从 src[i]（引号字符）开始，返回引号内容和结束后的下一位置。
func scanQuoted(src string, i int, quote byte) (string, int) {
	end := strings.IndexByte(src[i+1:], quote)
	if end >= 0 {
		return src[i+1 : i+1+end], i + end + 2
	}
	return src[i+1:], len(src)
}

// Scan 将整个源字符串（可含换行）扫描为平坦 Token 流，末尾附 EOF 哨兵。
//
// 规则：
//   - 换行折叠为单个 Newline token（空行/连续换行合并）
//   - # 注释延伸至行尾
//   - 单/双引号字符串 → Literal（引号去除）
//   - 路径 ./foo → Literal
//   - 数字 42, 3.14 → Literal
//   - <- / -> → Arrow
//   - 双字符算子 ==, !=, <=, >= 等 → Ident
//   - 单字符算子 +, -, *, / 等 → Ident
//   - 关键字 return/if/else/for/while → 对应 Kind
//   - 其余标识符（含点号，如 tensor.new）→ Ident
func Scan(src string) []Token {
	var tokens []Token
	i := 0
	prevNewline := true // 压制开头多余换行

	for i < len(src) {
		c := src[i]

		// 换行：折叠连续换行
		if c == '\n' || c == '\r' {
			if !prevNewline && len(tokens) > 0 {
				tokens = append(tokens, Token{Kind: Newline, Value: "\n"})
				prevNewline = true
			}
			i++
			continue
		}

		// 空白（非换行）
		if c == ' ' || c == '\t' {
			i++
			continue
		}

		// 单行注释
		if c == '#' {
			for i < len(src) && src[i] != '\n' {
				i++
			}
			continue
		}

		prevNewline = false

		// 引号字符串 → Literal（引号去除）
		if c == '\'' || c == '"' {
			val, next := scanQuoted(src, i, c)
			tokens = append(tokens, Token{Kind: Literal, Value: val})
			i = next
			continue
		}

		// 左箭头 <-（在双字符算子之前匹配，避免 <= 误判）
		if c == '<' && i+1 < len(src) && src[i+1] == '-' {
			tokens = append(tokens, Token{Kind: Arrow, Value: "<-"})
			i += 2
			continue
		}

		// 右箭头 ->
		if c == '-' && i+1 < len(src) && src[i+1] == '>' {
			tokens = append(tokens, Token{Kind: Arrow, Value: "->"})
			i += 2
			continue
		}

		// 双字符算子（在单字符之前匹配）
		if i+1 < len(src) {
			switch src[i : i+2] {
			case "==", "!=", "<=", ">=", "&&", "||", "<<", ">>":
				tokens = append(tokens, Token{Kind: Ident, Value: src[i : i+2]})
				i += 2
				continue
			}
		}

		// 单字符标点 — 查表
		if k, ok := singleCharToken[c]; ok {
			tokens = append(tokens, Token{Kind: k, Value: string(c)})
			i++
			continue
		}

		// 单字符符号算子
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

		// 数字字面量
		if c >= '0' && c <= '9' {
			start := i
			for i < len(src) && (src[i] >= '0' && src[i] <= '9' || src[i] == '.' || src[i] == 'e' || src[i] == 'E') {
				i++
			}
			tokens = append(tokens, Token{Kind: Literal, Value: src[start:i]})
			continue
		}

		// 路径字面量 ./foo（必须先于关键字读取）
		if c == '.' && i+1 < len(src) && src[i+1] == '/' {
			start := i
			i += 2
			for i < len(src) && !isTokenDelim(src[i]) {
				i++
			}
			tokens = append(tokens, Token{Kind: Literal, Value: src[start:i]})
			continue
		}

		// '/' — 除法算子或绝对路径（路径总是单引号引用，此处按算子处理）
		if c == '/' {
			tokens = append(tokens, Token{Kind: Ident, Value: "/"})
			i++
			continue
		}

		// 关键字 / 标识符（含点号：tensor.new 等）
		start := i
		for i < len(src) && !isTokenDelim(src[i]) {
			i++
		}
		if i == start {
			// 当前字符无匹配处理（如裸 =）→ 跳过，防止无限循环
			i++
			continue
		}
		word := src[start:i]
		switch word {
		case "return":
			tokens = append(tokens, Token{Kind: Return, Value: word})
		case "if":
			tokens = append(tokens, Token{Kind: If, Value: word})
		case "else":
			tokens = append(tokens, Token{Kind: Else, Value: word})
		case "for":
			tokens = append(tokens, Token{Kind: For, Value: word})
		case "while":
			tokens = append(tokens, Token{Kind: While, Value: word})
		default:
			tokens = append(tokens, Token{Kind: Ident, Value: word})
		}
	}

	tokens = append(tokens, Token{Kind: EOF, Value: ""})
	return tokens
}

// Tokenize 将一行 kvlang 代码分割为 Token 列表（不含 Newline/EOF）。
// 向后兼容接口，内部调用 Scan。
func Tokenize(line string) []Token {
	toks := Scan(line)
	out := toks[:0:len(toks)]
	for _, t := range toks {
		if t.Kind != Newline && t.Kind != EOF {
			out = append(out, t)
		}
	}
	return out
}

// isTokenDelim 判断字节是否为 Token 边界。
// 含 ':' 使 "A:int" 分割为 Ident("A") Colon(":") Ident("int")。
func isTokenDelim(c byte) bool {
	switch c {
	case ' ', '\t', '\n', '\r',
		',', ')', '(', '{', '}',
		'+', '-', '*', '%', '!', '=', '<', '>', '&', '|', '^',
		':':
		return true
	}
	return false
}
