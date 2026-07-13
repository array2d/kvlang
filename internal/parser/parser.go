// Package parser 提供 kvlang 语法分析：Token 流 → AST。
//
// 入口:
//   - ParseFile(path) → (*ast.File, []Diagnostic, error)   (文件级)
//   - ParseCode(r)    → (*ast.File, []Diagnostic, error)   (io.Reader 级)
//   - ParseFuncSig(sig) → ast.FuncSig                      (签名级，来自 KV 存储的字符串)
//
// 返回值约定：
//   - error != nil          → IO / 空输入等硬错误，*ast.File 为 nil
//   - error == nil, diags≠∅ → 语法错误，*ast.File 为尽力解析的部分结果
//   - error == nil, diags=∅ → 干净解析
//
// 数据流：io.Reader → Scan → []Token → parser → *ast.File
//
// 单向依赖：parser.go → stmt.go → inst.go → scanner.go
package parser

import (
	"fmt"
	"io"
	"os"
	"strings"

	"kvlang/internal/ast"
)

// ParseFile 打开并解析 .kv 源文件。
func ParseFile(path string) (*ast.File, []Diagnostic, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, nil, fmt.Errorf("open %s: %w", path, err)
	}
	defer f.Close()
	return ParseCode(f)
}

// ParseCode 从 io.Reader 解析 kvlang 代码。
// 语法错误通过 []Diagnostic 返回，不中断解析（error recovery）。
func ParseCode(r io.Reader) (*ast.File, []Diagnostic, error) {
	raw, err := io.ReadAll(r)
	if err != nil {
		return nil, nil, err
	}
	if strings.TrimSpace(string(raw)) == "" {
		return nil, nil, fmt.Errorf("empty input")
	}
	p := &parser{tokens: Scan(string(raw))}
	f := p.parseFile()
	if len(f.Funcs) == 0 {
		return nil, p.errors, fmt.Errorf("no 'def' found")
	}
	return f, p.errors, nil
}

// ── parser 结构体 ──────────────────────────────────────────────

type parser struct {
	tokens []Token
	pos    int
	errors []Diagnostic // 积累语法错误，不在第一个错误处停止
}

func (p *parser) peek() Token {
	if p.pos >= len(p.tokens) {
		return Token{Kind: EOF}
	}
	return p.tokens[p.pos]
}

func (p *parser) peekAt(offset int) Token {
	idx := p.pos + offset
	if idx < 0 || idx >= len(p.tokens) {
		return Token{Kind: EOF}
	}
	return p.tokens[idx]
}

func (p *parser) advance() Token {
	t := p.peek()
	if p.pos < len(p.tokens) {
		p.pos++
	}
	return t
}

func (p *parser) eat(k Kind) bool {
	if p.peek().Kind == k {
		p.advance()
		return true
	}
	return false
}

// expect 消费当前 Token。
// 若 Kind 不匹配：追加 Diagnostic，消费意外 token（不回退），返回合成 token 继续解析。
func (p *parser) expect(k Kind) Token {
	t := p.advance()
	if t.Kind != k && t.Kind != EOF {
		p.errors = append(p.errors, Diagnostic{
			Pos:     t.Pos,
			Message: fmt.Sprintf("expected %s, got %s %q", k, t.Kind, t.Value),
		})
		return Token{Kind: k, Pos: t.Pos} // 合成 token，让调用方继续
	}
	return t
}

func (p *parser) skipNewlines() {
	for p.peek().Kind == Newline {
		p.advance()
	}
}

// ── 文件级解析 ─────────────────────────────────────────────────

func (p *parser) parseFile() *ast.File {
	f := &ast.File{}
	for {
		p.skipNewlines()
		if p.peek().Kind == EOF {
			break
		}
		if p.peek().Kind == Ident && p.peek().Value == "def" {
			fn := p.parseFunc()
			f.Funcs = append(f.Funcs, fn)
		} else {
			inst := p.parseInst()
			if inst != nil && inst.Opcode != "" {
				f.TopLevelCalls = append(f.TopLevelCalls, inst)
			}
		}
	}
	return f
}

// parseFunc 解析单个函数定义：def name(...) -> (...) { body }
func (p *parser) parseFunc() ast.Func {
	sig := p.parseFuncSig()
	p.skipNewlines()
	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)
	return ast.Func{Sig: sig, Body: body}
}

// ── 签名解析 ───────────────────────────────────────────────────

// parseFuncSig 消费 def name(...) -> (...) 签名，直接构造 ast.FuncSig。
// 不经中间字符串，不需要 tokensToSig。
func (p *parser) parseFuncSig() ast.FuncSig {
	p.advance() // consume 'def'
	var sig ast.FuncSig
	if t := p.peek(); t.Kind == Ident {
		sig.Name = t.Value
		p.advance()
	}
	if p.peek().Kind == LParen {
		p.advance()
		sig.Params = p.parseParamList(RParen)
		p.expect(RParen)
	}
	if p.peek().Kind == Arrow {
		p.advance() // consume ->
		p.skipNewlines()
		if p.peek().Kind == LParen {
			p.advance()
			sig.Returns = p.parseParamList(RParen)
			p.expect(RParen)
		} else {
			sig.Returns = p.parseParamList(LBrace)
		}
	}
	return sig
}

// parseParamList 解析 param (, param)* 直到 stop Kind 为止（不消费 stop token）。
func (p *parser) parseParamList(stop Kind) []ast.Param {
	var params []ast.Param
	for p.peek().Kind != stop && p.peek().Kind != EOF {
		p.skipNewlines()
		if p.peek().Kind == stop {
			break
		}
		if p.eat(Comma) {
			continue
		}
		t := p.peek()
		if t.Kind != Ident && t.Kind != Literal {
			break
		}
		param := ast.Param{Name: p.advance().Value}
		if p.peek().Kind == Colon {
			p.advance()
			if p.peek().Kind == Ident {
				param.Type = p.advance().Value
			}
		}
		params = append(params, param)
	}
	return params
}

// ── 条件表达式解析 ─────────────────────────────────────────────

// parseCondInst 解析 if/while 括号内的条件表达式，直接构造 *ast.Instruction。
// 调用时已确认 peek() 为 LParen；返回后已消费 RParen。
func (p *parser) parseCondInst() *ast.Instruction {
	p.expect(LParen)
	inst := &ast.Instruction{}
	p.parseExprInto(inst)
	p.expect(RParen)
	return inst
}
