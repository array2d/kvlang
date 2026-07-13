// file.go: 文件级解析入口 + parser 核心结构体。
//
// 数据流：io.Reader → Scan → []Token → parser → *ast.File
package parser

import (
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
	raw, err := io.ReadAll(r)
	if err != nil {
		return nil, err
	}
	if strings.TrimSpace(string(raw)) == "" {
		return nil, fmt.Errorf("empty input")
	}
	p := &parser{tokens: Scan(string(raw))}
	f := p.parseFile()
	if len(f.Funcs) == 0 {
		return nil, fmt.Errorf("no 'def' found")
	}
	return f, nil
}

// ── parser 结构体 ──────────────────────────────────────────────

// parser 持有平坦 Token 流和当前读取位置。
type parser struct {
	tokens []Token
	pos    int
}

// peek 返回当前 Token，不消费；越界时返回 EOF 哨兵。
func (p *parser) peek() Token {
	if p.pos >= len(p.tokens) {
		return Token{Kind: EOF}
	}
	return p.tokens[p.pos]
}

// peekAt 返回从当前位置偏移 offset 处的 Token（不消费）。
func (p *parser) peekAt(offset int) Token {
	idx := p.pos + offset
	if idx < 0 || idx >= len(p.tokens) {
		return Token{Kind: EOF}
	}
	return p.tokens[idx]
}

// advance 消费并返回当前 Token。
func (p *parser) advance() Token {
	t := p.peek()
	if p.pos < len(p.tokens) {
		p.pos++
	}
	return t
}

// eat 若当前 Token 与 k 匹配则消费，返回是否成功。
func (p *parser) eat(k Kind) bool {
	if p.peek().Kind == k {
		p.advance()
		return true
	}
	return false
}

// expect 消费当前 Token；若 Kind 不匹配则直接消费并忽略（容错）。
func (p *parser) expect(k Kind) Token {
	t := p.advance()
	// 简单容错：不匹配时回退（使调用方仍能继续）
	if t.Kind != k && t.Kind != EOF {
		// 放回一步以减少级联错误
		if p.pos > 0 {
			p.pos--
		}
	}
	return t
}

// skipNewlines 跳过连续 Newline token。
func (p *parser) skipNewlines() {
	for p.peek().Kind == Newline {
		p.advance()
	}
}

// ── 文件级解析 ─────────────────────────────────────────────────

// parseFile 主解析循环：顺序消费 Token，遇 def 则 parseFunc，否则解析顶层调用。
func (p *parser) parseFile() *ast.File {
	f := &ast.File{}
	for {
		p.skipNewlines()
		t := p.peek()
		if t.Kind == EOF {
			break
		}
		if t.Kind == Ident && t.Value == "def" {
			fn := p.parseFunc()
			f.Funcs = append(f.Funcs, fn)
		} else {
			// 顶层指令（调用外部函数或内置函数）
			toks := p.collectInstTokens()
			if len(toks) == 0 {
				p.advance() // 容错：跳过无法识别的 token
				continue
			}
			inst, err := parseInstFromTokens(toks, "")
			if err == nil && inst != nil && inst.Opcode != "" {
				tc := ast.TopLevelCall{
					FuncName: inst.Opcode,
					Args:     inst.Reads,
					Outputs:  inst.Writes,
				}
				f.TopLevelCalls = append(f.TopLevelCalls, tc)
				f.PreambleLines = append(f.PreambleLines, inst.String())
			}
		}
	}
	return f
}

// parseFunc 解析单个函数定义：def name(...) -> (...) { body }
func (p *parser) parseFunc() ast.Func {
	// 收集签名 token：从 'def' 到 LBrace（不含）
	var sigToks []Token
	for p.peek().Kind != LBrace && p.peek().Kind != EOF {
		t := p.advance()
		if t.Kind != Newline { // 签名通常单行，忽略可能的换行
			sigToks = append(sigToks, t)
		}
	}

	name := nameFromSigToks(sigToks)
	sig := tokensToSig(sigToks)

	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)

	return ast.Func{Name: name, Signature: sig, Body: body}
}

// ── collectUntilRParen ──────────────────────────────────────────

// collectUntilRParen 消费从当前 LParen 到匹配 RParen（支持嵌套）之间的 Token，
// 返回空格拼接的字符串（用于 if/for/while 条件）。
func (p *parser) collectUntilRParen() string {
	p.expect(LParen)
	var parts []string
	depth := 1
	for depth > 0 && p.peek().Kind != EOF {
		t := p.advance()
		switch t.Kind {
		case LParen:
			depth++
			parts = append(parts, t.Value)
		case RParen:
			depth--
			if depth > 0 {
				parts = append(parts, t.Value)
			}
		default:
			parts = append(parts, t.Value)
		}
	}
	return strings.Join(parts, " ")
}

// ── 签名重建辅助 ───────────────────────────────────────────────

// nameFromSigToks 从签名 token 列表中提取函数名。
// 签名以 Ident("def") 开头，函数名为紧随其后的 Ident。
func nameFromSigToks(toks []Token) string {
	for i, t := range toks {
		if t.Kind == Ident && t.Value == "def" {
			if i+1 < len(toks) && toks[i+1].Kind == Ident {
				return toks[i+1].Value
			}
		}
	}
	return ""
}

// tokensToSig 将签名 token 列表重建为规范字符串。
//
// 空格规则（与 kvlang 惯例一致）：
//   - ) , : 前不加空格
//   - ( 前不加空格（除非紧跟 ->）
//   - ( : 后不加空格
func tokensToSig(toks []Token) string {
	var sb strings.Builder
	for i, t := range toks {
		if i > 0 {
			prev := toks[i-1]
			addSpace := true
			// 前无空格：) , :
			if t.Kind == RParen || t.Kind == Comma || t.Kind == Colon {
				addSpace = false
			}
			// 前无空格：( ，除非 -> 后的 (
			if t.Kind == LParen && prev.Kind != Arrow {
				addSpace = false
			}
			// 后无空格：( :
			if prev.Kind == LParen || prev.Kind == Colon {
				addSpace = false
			}
			if addSpace {
				sb.WriteByte(' ')
			}
		}
		sb.WriteString(t.Value)
	}
	return sb.String()
}
