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
		lines := strings.Split(string(raw), "\n")
		p := &parser{tokens: Scan(string(raw)), srcLines: lines, srcName: "<inline>"}
	f := p.parseFile()
	// 为每个 diagnostic 绑定出错行源码（fix-030）
	for i := range p.errors {
		d := &p.errors[i]
		if d.Pos.Line > 0 && d.Pos.Line <= len(lines) {
			d.Source = lines[d.Pos.Line-1]
		}
		d.SrcName = "<inline>"
	}
	return f, p.errors, nil
}

// ── parser 结构体 ──────────────────────────────────────────────

type parser struct {
	tokens     []Token
	pos        int
	errors     []Diagnostic // 积累语法错误，不在第一个错误处停止
	srcLines   []string     // 源码行缓存（fix-030：为 diagnostic 附加出错行上下文）
	srcName    string       // 文件名/内联标注（fix-030）
	srcAliases map[string]string // import … as 别名映射（fix-035：parsePrimaryExpr dotted call 全路径还原用）
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
// 若 Kind 不匹配：追加 Diagnostic，消费意外 token（error recovery），返回合成 token。
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

// skipNewlines 跳过连续 Newline Token（不消费 Comment）。
func (p *parser) skipNewlines() {
	for p.peek().Kind == Newline {
		p.advance()
	}
}

// skipNewlinesAndComments 跳过连续 Newline 和 Comment Token（内容丢弃）。
// 用于结构性上下文（def body 之前、else 之前等），不需要保留注释。
func (p *parser) skipNewlinesAndComments() {
	for {
		k := p.peek().Kind
		if k == Newline || k == Comment {
			p.advance()
		} else {
			break
		}
	}
}

// collectLeadingComments 消费并返回前置 Comment 和 Newline Token。
// 用于 parseBody / parseFile，将注释附加到紧随其后的 AST 节点（S6：注释保留）。
func (p *parser) collectLeadingComments() []string {
	var comments []string
	for {
		switch p.peek().Kind {
		case Newline:
			p.advance()
		case Comment:
			comments = append(comments, p.advance().Value)
		default:
			return comments
		}
	}
}

// ── 文件级解析 ─────────────────────────────────────────────────

func (p *parser) parseFile() *ast.File {
	f := &ast.File{}
	for {
		comments := p.collectLeadingComments()
		if p.peek().Kind == EOF {
			break
		}
		// import pkg（kvspace）或 import "path"（文件糖）或 import … as alias（fix-033/035）
		if p.peek().Kind == Ident && p.peek().Value == "import" {
			p.advance() // consume "import"
			var imp string
			isQuoted := false
			// import "path/to/file.kv" — 文件系统路径
			if p.peek().Kind == Literal && p.peek().Quote == '"' {
				imp = p.advance().Value
				isQuoted = true
			} else if p.peek().Kind == Ident {
				// import pkg — kvspace /lib/<pkg>/ 包引用
				imp = p.advance().Value
			} else {
				p.errors = append(p.errors, Diagnostic{
					Pos: p.peek().Pos, Warn: true,
					Message: "import requires a package name or double-quoted file path",
				})
				p.eat(Newline)
				continue
			}
			// import … as alias（fix-035）
			if p.peek().Kind == Ident && p.peek().Value == "as" {
				p.advance() // consume "as"
				if p.peek().Kind == Ident {
					if f.Aliases == nil { f.Aliases = map[string]string{} }
					alias := p.advance().Value
					if f.Aliases == nil { f.Aliases = map[string]string{} }
					f.Aliases[alias] = imp
					if p.srcAliases == nil { p.srcAliases = map[string]string{} }
					p.srcAliases[alias] = imp
				}
			}
			f.Imports = append(f.Imports, imp)
			if isQuoted { f.ImportPaths = append(f.ImportPaths, imp) }
			p.eat(Newline)
			continue
		}
		// lib name { ... } — 命名空间块（fix-034：借鉴 C++ namespace / Rust mod）
		if p.peek().Kind == Ident && p.peek().Value == "lib" && p.peekAt(1).Kind == Ident && p.peekAt(2).Kind == LBrace {
			p.advance() // consume "lib"
			pkg := p.advance().Value
			if pkg == "lib" {
				p.errors = append(p.errors, Diagnostic{Pos: p.peek().Pos, Warn: true,
					Message: fmt.Sprintf("package name %q expands to /lib/lib/ — consider a different name", pkg)})
			}
			if f.Package == "" { f.Package = pkg }
			p.expect(LBrace)
			p.skipNewlines()
			for p.peek().Kind != RBrace && p.peek().Kind != EOF {
				if p.peek().Kind == Ident && p.peek().Value == "def" {
					fn := p.parseFunc()
					f.Funcs = append(f.Funcs, fn)
				} else { break }
				p.skipNewlines()
			}
			p.expect(RBrace)
			continue
		}
		// init { ... } — 初始化块（fix-033）
		if p.peek().Kind == Ident && p.peek().Value == "init" && p.peekAt(1).Kind == LBrace {
			p.advance() // consume "init"
			p.advance() // consume {
			p.skipNewlines()
			for p.peek().Kind != RBrace && p.peek().Kind != EOF {
				st := p.parseStmt()
				if st != nil { f.InitBody = append(f.InitBody, st) }
				p.skipNewlines()
			}
			p.expect(RBrace)
			continue
		}
		if p.peek().Kind == Ident && p.peek().Value == "def" {
			if f.Package == "" {
				p.errors = append(p.errors, Diagnostic{Pos: p.peek().Pos, Warn: true,
					Message: fmt.Sprintf("def outside lib block — registering under /lib/<name>; consider wrapping in 'lib pkgname { }'")})
			}
			fn := p.parseFunc()
			fn.Comments = comments
			f.Funcs = append(f.Funcs, fn)
		} else {
			prevPos := p.pos
			inst := p.parseInst()
			if inst != nil && inst.Expr != nil {
				inst.Comments = comments
				f.TopLevelCalls = append(f.TopLevelCalls, inst)
			} else if p.pos == prevPos {
				// 解析无进展（如悬挂的 ')'）：跳过一个 token，防止死循环
				if p.peek().Kind != EOF {
					p.errors = append(p.errors, Diagnostic{
						Pos:     p.peek().Pos,
						Message: fmt.Sprintf("unexpected token %s %q at top level", p.peek().Kind, p.peek().Value),
					})
					p.advance()
				}
			}
		}
	}
	return f
}

// parseFunc 解析单个函数定义：def name(...) -> (...) { body }
func (p *parser) parseFunc() ast.Func {
	sig := p.parseFuncSig()
	p.checkParamDup(&sig)
	p.skipNewlinesAndComments()
	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)
	fn := ast.Func{Sig: sig, Body: body}
	p.checkReadOnlyParams(&fn)
	return fn
}

// checkParamDup 参数不可同名，尤其读参列表与写参列表之间（fix-032）。
// def f(A:int) -> (A:int) 中 A 同时是读参又是写参 → 签名非法。
func (p *parser) checkParamDup(sig *ast.FuncSig) {
	seen := map[string]bool{}
	for _, param := range sig.ParamNames() {
		seen[param] = true
	}
	for _, ret := range sig.Returns {
		if seen[ret.Name] {
			p.errors = append(p.errors, Diagnostic{Message: fmt.Sprintf(
				"func %s: param %q appears in both read-params and write-params — a param is either read-only or write-only, pick one",
				sig.Name, ret.Name)})
		}
		seen[ret.Name] = true
	}
}

// checkReadOnlyParams 读参只读公理（fix-027）：读参是「调用方 → 被调方」的输入绑定，
// 函数体内任何指令/子函数调用把读参裸名放进写槽 = 破坏读写码数据流方向 → error 级诊断。
// 豁免：成员写脱糖形态 set(base, k, v) -> base 的本体回写（写回原值，见 fix-013）；
// A.x / A[i] 写的是成员键非本体，脱糖后即上述 set 形态。
func (p *parser) checkReadOnlyParams(fn *ast.Func) {
	ro := map[string]bool{}
	for _, n := range fn.Sig.ParamNames() {
		ro[n] = true
	}
	if len(ro) == 0 {
		return
	}
	bad := func(w string) {
		// AST 遍历无 token span，标注函数体第一行（fix-030 定位需求）
		p.errors = append(p.errors, Diagnostic{Pos: Pos{Line: 1, Col: 1}, Message: fmt.Sprintf(
			"func %s: read param %q cannot be used as write slot (read params are read-only)",
			fn.Sig.Name, w)})
	}
	check := func(inst *ast.Instruction) {
		for i, w := range inst.Writes {
			if strings.ContainsAny(w, "/.[") {
				continue // 路径 / 成员键 / 下标形态：非本体写
			}
			if inst.Expr != nil && inst.Expr.Op == "set" && i == 0 &&
				len(inst.Expr.Args) > 0 && w == inst.Expr.Args[0].Val {
				continue // set 本体回写豁免
			}
			if ro[w] {
				bad(w)
			}
		}
	}
	var walk func(body []ast.Stmt)
	walk = func(body []ast.Stmt) {
		for _, st := range body {
			switch s := st.(type) {
			case *ast.Instruction:
				check(s)
			case *ast.IfStmt:
				walk(s.Then)
				walk(s.Else)
			case *ast.WhileStmt:
				walk(s.Body)
			case *ast.ForStmt:
				if ro[s.Var] {
					bad(s.Var)
				}
				walk(s.Body)
			case *ast.BlockStmt:
				walk(s.Body)
			}
		}
	}
	walk(fn.Body)
}

// HasErrors 报告诊断中是否存在 error 级（非 Warn）条目。
func HasErrors(diags []Diagnostic) bool {
	for _, d := range diags {
		if !d.Warn {
			return true
		}
	}
	return false
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

// ParseFuncSig 将签名字符串解析为 ast.FuncSig（公开 API）。
// 签名格式为 KV 中存储的 FuncSig.String() 输出：def name(A:t) -> (B:t)
func ParseFuncSig(sig string) ast.FuncSig {
	toks := Scan(sig)
	p := &parser{tokens: toks}
	return p.parseFuncSig()
}
