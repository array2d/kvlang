// stmt.go: 语句级解析。
//
// parseBody / parseStmt → parseIf / parseFor / parseWhile / parseBlockLabel
// parseStmt default → parseInst（Pratt 流式，在 inst.go）
//
// 单向依赖：parser.go → stmt.go → inst.go → scanner.go
package parser

import (
	"kvlang/internal/ast"
)

// parseBody 消费 Token 直到 RBrace 或 EOF，返回语句列表。
// 每条语句前收集前置行注释（S6：注释保留）并附加到对应节点。
func (p *parser) parseBody() []ast.Stmt {
	var stmts []ast.Stmt
	for {
		comments := p.collectLeadingComments()
		t := p.peek()
		if t.Kind == RBrace || t.Kind == EOF {
			break
		}
		st := p.parseStmt()
		if st != nil {
			attachComments(st, comments)
			stmts = append(stmts, st)
		}
	}
	return stmts
}

// parseStmt 根据首 Token 分发到对应语句解析函数。
func (p *parser) parseStmt() ast.Stmt {
	// 块标签检测（优先级最高）：Ident 后紧跟 Colon → "label: { body }"
	if p.peekAt(1).Kind == Colon {
		return p.parseBlockLabel()
	}

	switch p.peek().Kind {
	case If:
		return p.parseIf()
	case For:
		return p.parseFor()
	case While:
		return p.parseWhile()
	case Break:
		p.advance()
		p.eat(Newline)
		return &ast.BreakStmt{}
	case Continue:
		p.advance()
		p.eat(Newline)
		return &ast.ContinueStmt{}
	}
	// 其余情况：普通指令（Pratt 流式解析，无中间 buffer）
	return p.parseInst()
}

// parseIf 解析 if/else 块：if (cond) { then } [else { else }]
func (p *parser) parseIf() *ast.IfStmt {
	p.advance() // consume 'if'
	cond := p.parseCondInst()
	p.skipNewlinesAndComments()
	p.expect(LBrace)
	then := p.parseBody()
	p.expect(RBrace)

	p.skipNewlinesAndComments()
	if p.peek().Kind == Else {
		p.advance()
		p.skipNewlinesAndComments()
		p.expect(LBrace)
		els := p.parseBody()
		p.expect(RBrace)
		return &ast.IfStmt{Cond: cond, Then: then, Else: els}
	}
	return &ast.IfStmt{Cond: cond, Then: then}
}

// parseFor 解析 for 循环：for (var[:type] in iter_path) { body }
func (p *parser) parseFor() *ast.ForStmt {
	p.advance() // consume 'for'
	p.expect(LParen)

	// 迭代变量名（可选 :type 标注）
	varName := ""
	if t := p.peek(); t.Kind == Ident || t.Kind == Literal {
		varName = p.advance().Value
		if p.peek().Kind == Colon {
			p.advance() // consume :
			if p.peek().Kind == Ident {
				p.advance() // consume type（暂忽略）
			}
		}
	}

	// 'in' 关键字
	if p.peek().Kind == Ident && p.peek().Value == "in" {
		p.advance()
	}

	// 迭代路径
	iter := ""
	if p.peek().Kind != RParen && p.peek().Kind != EOF {
		iter = p.advance().Value
	}

	p.expect(RParen)
	p.skipNewlinesAndComments()
	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)

	return &ast.ForStmt{Var: varName, Iter: iter, Body: body}
}

// parseWhile 解析 while 循环：while (cond) { body }
func (p *parser) parseWhile() *ast.WhileStmt {
	p.advance() // consume 'while'
	cond := p.parseCondInst()
	p.skipNewlinesAndComments()
	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)
	return &ast.WhileStmt{Cond: cond, Body: body}
}

// parseBlockLabel 解析带标签的基本块：label: { body }
func (p *parser) parseBlockLabel() *ast.BlockStmt {
	label := p.advance().Value // consume Ident（标签名）
	p.advance()                // consume Colon
	p.skipNewlinesAndComments()
	p.expect(LBrace)
	body := p.parseBody()
	p.expect(RBrace)
	return &ast.BlockStmt{Label: label, Body: body}
}

// attachComments 将 comments 附加到语句节点的 Comments 字段。
func attachComments(st ast.Stmt, comments []string) {
	if len(comments) == 0 {
		return
	}
	switch s := st.(type) {
	case *ast.Instruction:
		s.Comments = comments
	case *ast.IfStmt:
		s.Comments = comments
	case *ast.ForStmt:
		s.Comments = comments
	case *ast.WhileStmt:
		s.Comments = comments
	case *ast.BreakStmt:
		s.Comments = comments
	case *ast.ContinueStmt:
		s.Comments = comments
	case *ast.BlockStmt:
		s.Comments = comments
	}
}
