// scanner.go: kvlang 词法分析。
// Scan(src string) → []Token  支持整文件多行扫描，末尾附 EOF 哨兵。
package parser

import (
	"fmt"
	"strings"
)

// Pos 携带 Token 在源码中的起始位置。
type Pos struct {
	Line int // 1-based
	Col  int // 1-based
}

// Diagnostic 表示一条解析错误，携带位置信息。
type Diagnostic struct {
	Pos     Pos
	Message string
}

func (d Diagnostic) String() string {
	return fmt.Sprintf("%d:%d: %s", d.Pos.Line, d.Pos.Col, d.Message)
}

// Kind 标记 Token 类型。
type Kind int

const (
	Ident    Kind = iota // 标识符、符号算子、未引用数字/路径段
	Literal              // 字面量（引号已去除）
	Arrow                // -> 或 <-
	LParen               // (
	RParen               // )
	Comma                // ,
	LBrace               // {
	RBrace               // }
	Colon                // :
	Return               // return
	If                   // if
	Else                 // else
	For                  // for
	While                // while
	Break                // break
	Continue             // continue
	Newline              // 换行（语句分隔符，连续换行折叠为一个）
	Comment              // # 行注释（含 # 字符）
	EOF                  // 文件结束哨兵
)

// String 返回 Kind 的可读名称。
func (k Kind) String() string {
	names := [...]string{
		"IDENT", "LITERAL", "ARROW",
		"LPAREN", "RPAREN", "COMMA", "LBRACE", "RBRACE", "COLON",
		"RETURN", "IF", "ELSE", "FOR", "WHILE",
		"BREAK", "CONTINUE",
		"NEWLINE", "COMMENT", "EOF",
	}
	if int(k) < len(names) {
		return names[k]
	}
	return "UNKNOWN"
}

// Token 表示一个词法单元，携带源码起始位置。
type Token struct {
	Kind  Kind
	Value string
	Pos   Pos
}

// String 返回 Token 的调试表示。
func (t Token) String() string {
	return fmt.Sprintf("%s(%q)@%d:%d", t.Kind, t.Value, t.Pos.Line, t.Pos.Col)
}

// 语言关键字字符串常量（供 keywords 表和注释使用）。
const kwReturn   = "return"
const kwIf       = "if"
const kwElse     = "else"
const kwFor      = "for"
const kwWhile    = "while"
const kwBreak    = "break"
const kwContinue = "continue"

// keywords 将语言关键字映射到对应 Token Kind。
var keywords = map[string]Kind{
	kwReturn:   Return,
	kwIf:       If,
	kwElse:     Else,
	kwFor:      For,
	kwWhile:    While,
	kwBreak:    Break,
	kwContinue: Continue,
}

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
// 每个 Token 携带源码起始位置 Pos{Line, Col}（均 1-based）。
// # 注释产生 Comment Token，格式化工具可通过 Comments 字段保留注释（S6）。
// 连续换行折叠为一个 Newline Token。
func Scan(src string) []Token {
	var tokens []Token
	i := 0
	line := 1
	lineStart := 0  // 当前行起始字节偏移
	prevNewline := true

	// pos 返回当前字节偏移 i 对应的源码位置。
	pos := func() Pos { return Pos{Line: line, Col: i - lineStart + 1} }

	for i < len(src) {
		c := src[i]

		// 换行：折叠连续换行
		if c == '\n' {
			if !prevNewline && len(tokens) > 0 {
				tokens = append(tokens, Token{Kind: Newline, Value: "\n", Pos: pos()})
				prevNewline = true
			}
			i++
			line++
			lineStart = i
			continue
		}
		if c == '\r' {
			i++
			continue
		}

		// 空白（非换行）
		if c == ' ' || c == '\t' {
			i++
			continue
		}

		// ';' — 显式语句分隔符，折叠规则与 '\n' 相同
		if c == ';' {
			if !prevNewline && len(tokens) > 0 {
				tokens = append(tokens, Token{Kind: Newline, Value: ";", Pos: pos()})
				prevNewline = true
			}
			i++
			continue
		}

		// # 行注释 → Comment Token（S6：保留注释）
		if c == '#' {
			p := pos()
			start := i
			for i < len(src) && src[i] != '\n' {
				i++
			}
			tokens = append(tokens, Token{Kind: Comment, Value: src[start:i], Pos: p})
			prevNewline = false // Comment 是实质 token，后续 \n 会触发 Newline
			continue
		}

		prevNewline = false
		p := pos() // 记录当前 token 起始位置

		// 引号字符串 → Literal（引号去除）
		if c == '\'' || c == '"' {
			val, next := scanQuoted(src, i, c)
			tokens = append(tokens, Token{Kind: Literal, Value: val, Pos: p})
			i = next
			continue
		}

		// 左箭头 <-
		if c == '<' && i+1 < len(src) && src[i+1] == '-' {
			tokens = append(tokens, Token{Kind: Arrow, Value: "<-", Pos: p})
			i += 2
			continue
		}

		// 右箭头 ->
		if c == '-' && i+1 < len(src) && src[i+1] == '>' {
			tokens = append(tokens, Token{Kind: Arrow, Value: "->", Pos: p})
			i += 2
			continue
		}

		// 双字符算子（在单字符之前匹配）
		if i+1 < len(src) {
			switch src[i : i+2] {
			case "==", "!=", "<=", ">=", "&&", "||", "<<", ">>":
				tokens = append(tokens, Token{Kind: Ident, Value: src[i : i+2], Pos: p})
				i += 2
				continue
			}
		}

		// 单字符标点 — 查表
		if k, ok := singleCharToken[c]; ok {
			tokens = append(tokens, Token{Kind: k, Value: string(c), Pos: p})
			i++
			continue
		}

		// 单字符符号算子
		switch c {
		case '+', '*', '%', '!', '<', '>', '&', '|', '^':
			tokens = append(tokens, Token{Kind: Ident, Value: string(c), Pos: p})
			i++
			continue
		}

		// '-' 单独处理（已排除 -> 的情况）
		if c == '-' {
			tokens = append(tokens, Token{Kind: Ident, Value: "-", Pos: p})
			i++
			continue
		}

		// 数字字面量
		if c >= '0' && c <= '9' {
			start := i
			for i < len(src) && (src[i] >= '0' && src[i] <= '9' || src[i] == '.' || src[i] == 'e' || src[i] == 'E') {
				i++
			}
			tokens = append(tokens, Token{Kind: Literal, Value: src[start:i], Pos: p})
			continue
		}

		// 路径字面量 ./foo
		if c == '.' && i+1 < len(src) && src[i+1] == '/' {
			start := i
			i += 2
			for i < len(src) && !isTokenDelim(src[i]) {
				i++
			}
			tokens = append(tokens, Token{Kind: Literal, Value: src[start:i], Pos: p})
			continue
		}

		// '/' — 绝对路径字面量 或 除法算子
		if c == '/' {
			if i+1 < len(src) && isAbsPathStart(src[i+1]) {
				start := i
				i++
				for i < len(src) && !isTokenDelim(src[i]) {
					i++
				}
				tokens = append(tokens, Token{Kind: Literal, Value: src[start:i], Pos: p})
			} else {
				tokens = append(tokens, Token{Kind: Ident, Value: "/", Pos: p})
				i++
			}
			continue
		}

		// 关键字 / 标识符（含点号：tensor.new 等）
		start := i
		for i < len(src) && !isTokenDelim(src[i]) {
			i++
		}
		if i == start {
			i++ // 跳过无法识别的字符，防止无限循环
			continue
		}
		word := src[start:i]
		if k, ok := keywords[word]; ok {
			tokens = append(tokens, Token{Kind: k, Value: word, Pos: p})
		} else {
			tokens = append(tokens, Token{Kind: Ident, Value: word, Pos: p})
		}
	}

	tokens = append(tokens, Token{Kind: EOF, Value: "", Pos: pos()})
	return tokens
}

func isTokenDelim(c byte) bool {
	switch c {
	case ' ', '\t', '\n', '\r', ';',
		',', ')', '(', '{', '}',
		'+', '-', '*', '%', '!', '=', '<', '>', '&', '|', '^',
		':':
		return true
	}
	return false
}

func isAbsPathStart(c byte) bool {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_'
}
