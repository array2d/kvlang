// stmt.go: 语句级解析。
//
// parseBody / parseStmt → parseIf / parseFor / parseWhile / parseBlockLabel
// collectInstTokens → parseInstFromTokens（在 expr.go）
//
// 单向依赖：stmt.go → expr.go → scanner.go
package parser

import (
	"strings"

	"kvlang/internal/ast"
)

// ── parseBody / parseStmt ──────────────────────────────────────

// parseBody 消费 Token 直到 RBrace 或 EOF，返回语句列表。
// 调用方负责消费 LBrace（进入块前）和 RBrace（退出块后）。
func (p *parser) parseBody() []ast.Stmt {
	var stmts []ast.Stmt
	for {
		p.skipNewlines()
		t := p.peek()
		if t.Kind == RBrace || t.Kind == EOF {
			break
		}
		st := p.parseStmt()
		if st != nil {
			stmts = append(stmts, st)
		}
	}
	return stmts
}

// parseStmt 根据首 Token 分发到对应语句解析函数。
func (p *parser) parseStmt() ast.Stmt {
	t := p.peek()

	// 块标签检测（优先级最高）：任意标记后紧跟 Colon → "label: { body }"
	// 这允许 "else:"、"return:" 等关键字也用作块标签。
	if p.peekAt(1).Kind == Colon {
		return p.parseBlockLabel()
	}

	switch t.Kind {
	case If:
		return p.parseIf()
	case For:
		return p.parseFor()
	case While:
		return p.parseWhile()
	case Ident:
		switch t.Value {
		case "break":
			p.advance()
			return &ast.BreakStmt{}
		case "continue":
			p.advance()
			return &ast.ContinueStmt{}
		}
	}
	// 其余情况：普通指令
	toks := p.collectInstTokens()
	if len(toks) == 0 {
		return nil
	}
	inst, _ := parseInstFromTokens(toks, "")
	return inst
}

// ── 控制流语句 ─────────────────────────────────────────────────

// parseIf 解析 if/else 块：if (cond) { then } [else { else }]
func (p *parser) parseIf() *ast.IfStmt {
	p.advance() // consume 'if'
	cond := p.collectUntilRParen()
	p.skipNewlines()
	p.expect(LBrace)
	then := p.parseBody()
	p.expect(RBrace)

	p.skipNewlines()
	if p.peek().Kind == Else {
		p.advance() // consume 'else'
		p.skipNewlines()
		p.expect(LBrace)
		els := p.parseBody()
		p.expect(RBrace)
		return &ast.IfStmt{Cond: cond, Then: then, Else: els}
	}
	return &ast.IfStmt{Cond: cond, Then: then}
}

// parseFor 解析 for 循环：for (var in iter_path) { body }
func (p *parser) parseFor() *ast.ForStmt {
	p.advance() // consume 'for'
	cond := p.collectUntilRParen()

	// 拆分 "var[:type] in iter"
	parts := strings.SplitN(cond, " in ", 2)
	varName := strings.TrimSpace(parts[0])
	if colon := strings.Index(varName, ":"); colon >= 0 {
		varName = strings.TrimSpace(varName[:colon])
	}
	iter := ""
	if len(parts) == 2 {
		iter = strings.TrimSpace(parts[1])
	}

	p.skipNewlines()
	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)

	return &ast.ForStmt{Var: varName, Iter: iter, Body: body}
}

// parseWhile 解析 while 循环：while (cond) { body }
func (p *parser) parseWhile() *ast.WhileStmt {
	p.advance() // consume 'while'
	cond := p.collectUntilRParen()
	p.skipNewlines()
	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)
	return &ast.WhileStmt{Cond: cond, Body: body}
}

// parseBlockLabel 解析带标签的基本块：label: { body }
// 调用前调用方已确认 peek()==Ident && peekAt(1)==Colon。
func (p *parser) parseBlockLabel() *ast.BlockStmt {
	label := p.advance().Value // consume Ident（标签名）
	p.advance()                // consume Colon
	p.skipNewlines()
	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)
	return &ast.BlockStmt{Label: label, Body: body}
}

// ── 指令 token 收集 ────────────────────────────────────────────

// collectInstTokens 收集构成一条指令的 Token 列表。
//
// 停止条件（深度为 0）：
//   - Newline  — 语句行结束
//   - RBrace   — 块结束
//   - EOF      — 文件结束
//   - 新语句关键字（在已有 token 之后出现）— 安全兜底
func (p *parser) collectInstTokens() []Token {
	var toks []Token
	depth := 0
	for {
		t := p.peek()
		switch t.Kind {
		case EOF, RBrace:
			return toks
		case Newline:
			p.advance() // 消费换行，离开后调用方处于下一语句首
			return toks
		}
		// 深度 0 且已有 token：遇到新语句起始则停止（安全兜底）
		if depth == 0 && len(toks) > 0 && isNewStmtStart(t) {
			return toks
		}
		toks = append(toks, p.advance())
		switch t.Kind {
		case LParen:
			depth++
		case RParen:
			depth--
		}
	}
}

// isNewStmtStart 判断 Token 是否为新语句的起始（用于 collectInstTokens 的安全停止）。
func isNewStmtStart(t Token) bool {
	switch t.Kind {
	case If, For, While, Return:
		return true
	case Ident:
		switch t.Value {
		case "break", "continue", "def":
			return true
		}
	}
	return false
}
