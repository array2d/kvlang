// inst.go: 指令级解析——Pratt 解析器（S3：表达式优先级）。
//
// (p *parser).parseInst() / parseCondInst() 直接在 token 流上解析，
// 无中间 token buffer，无启发式停止条件。
package parser

import (
	"kvlang/internal/ast"
)

// parseInst 直接在 token 流上解析一条指令（Pratt 递归下降）。
// 停止于 Newline / RBrace / EOF（自然语句边界）。
//
// 支持三种形式：
//   (writes) <- expr   写槽在左
//   expr -> (writes)   写槽在右
//   expr               无写槽（纯表达式 / 函数调用）
func (p *parser) parseInst() *ast.Instruction {
	inst := &ast.Instruction{}

	// 前瞻：找到第一个顶层 Arrow（不在括号内）
	arrowAbs, arrowVal := p.findTopLevelArrow()

	switch {
	case arrowAbs >= 0 && arrowVal == "<-":
		// (writes) <- expr
		inst.Writes = p.collectWritesUntilArrow()
		p.advance() // consume <-
		inst.Expr = p.parsePratt(0)

	case arrowAbs >= 0:
		// expr -> (writes)
		inst.Expr = p.parsePratt(0)
		p.advance() // consume ->
		inst.Writes = p.collectWriteList()

	default:
		// 无 Arrow：纯表达式 / 函数调用
		inst.Expr = p.parsePratt(0)
	}

	// 吃掉行尾内联注释（不保留，不会影响下一语句的前置注释收集）
	if p.peek().Kind == Comment {
		p.advance()
	}
	p.eat(Newline)
	return inst
}

// findTopLevelArrow 前瞻（不消费）找第一个深度为 0 的 Arrow token 绝对下标。
// 遇 Newline / RBrace / EOF / Comment 停止；返回 (-1, "") 表示未找到。
func (p *parser) findTopLevelArrow() (int, string) {
	depth := 0
	for i := p.pos; i < len(p.tokens); i++ {
		switch p.tokens[i].Kind {
		case LParen:
			depth++
		case RParen:
			depth--
		case Arrow:
			if depth == 0 {
				return i, p.tokens[i].Value
			}
		case Newline, RBrace, EOF, Comment:
			return -1, ""
		}
	}
	return -1, ""
}

// ── Pratt 解析器 ──────────────────────────────────────────────

// unaryPrec 一元前缀算子的"绑定力"（高于所有中缀算子）。
const unaryPrec = 150

// parsePratt 实现 Pratt（Top-Down Operator Precedence）解析器。
// 解析优先级 > minPrec 的中缀算子组成的表达式，并递归构建 Expr 树。
func (p *parser) parsePratt(minPrec int) *ast.Expr {
	left := p.parsePrimaryExpr()
	if left == nil {
		return nil
	}
	for {
		t := p.peek()
		// 只有 Ident 类型的已知中缀算子才能延伸表达式
		if t.Kind != Ident {
			break
		}
		prec := ast.InfixPrec(t.Value)
		if prec == 0 || prec <= minPrec {
			break
		}
		op := p.advance().Value // 消费中缀算子
		right := p.parsePratt(prec) // 左结合：右侧需严格更高
		left = ast.Call(op, left, right)
	}
	return left
}

// parsePrimaryExpr 解析主表达式（一元前缀、括号分组、函数调用、叶节点）。
func (p *parser) parsePrimaryExpr() *ast.Expr {
	t := p.peek()

	// 停止条件：自然边界或分隔符
	switch t.Kind {
	case Arrow, RParen, Newline, RBrace, EOF, Comma, Comment:
		return nil
	}

	// 一元前缀算子：! 或 - 后跟操作数
	if t.Kind == Ident && isUnaryPrefixOp(t.Value) {
		p.advance()
		arg := p.parsePratt(unaryPrec)
		return ast.Call(t.Value, arg)
	}

	// 括号分组：(expr)
	if t.Kind == LParen {
		p.advance()
		expr := p.parsePratt(0)
		p.expect(RParen)
		return expr
	}

	// 函数调用：name(arg, ...) — name 可为任意非停止 token（含 return 等关键字）
	if p.peekAt(1).Kind == LParen {
		name := p.advance().Value
		p.advance() // consume (
		var args []*ast.Expr
		for p.peek().Kind != RParen && p.peek().Kind != EOF {
			if p.eat(Comma) {
				continue
			}
			arg := p.parsePratt(0)
			if arg != nil {
				args = append(args, arg)
			}
		}
		p.expect(RParen)
		return ast.Call(name, args...)
	}

	// 叶节点：变量名、字面量、路径、裸操作码
	return ast.Leaf(p.advance().Value)
}

// ── 写槽收集 ──────────────────────────────────────────────────

// collectWriteList 收集 -> 右侧的写槽列表。
// 支持 (a, b) 带括号形式和裸 a[, b...] 形式。
func (p *parser) collectWriteList() []string {
	if p.peek().Kind == LParen {
		p.advance() // consume (
		var writes []string
		for p.peek().Kind != RParen && p.peek().Kind != EOF {
			if p.eat(Comma) {
				continue
			}
			writes = append(writes, p.advance().Value)
		}
		p.expect(RParen)
		return writes
	}
	var writes []string
	for {
		t := p.peek()
		if t.Kind == Newline || t.Kind == RBrace || t.Kind == EOF ||
			t.Kind == RParen || t.Kind == Comment {
			break
		}
		if t.Kind == Comma {
			p.advance()
			continue
		}
		writes = append(writes, p.advance().Value)
	}
	return writes
}

// collectWritesUntilArrow 收集 <- 左侧的写槽，直到遇到 Arrow 为止（不消费 Arrow）。
func (p *parser) collectWritesUntilArrow() []string {
	hasParen := p.peek().Kind == LParen
	if hasParen {
		p.advance() // consume (
	}
	var writes []string
	for {
		t := p.peek()
		if t.Kind == Arrow || t.Kind == EOF || t.Kind == Comment {
			break
		}
		if hasParen && t.Kind == RParen {
			p.advance() // consume )
			break
		}
		if t.Kind == Comma {
			p.advance()
			continue
		}
		writes = append(writes, p.advance().Value)
	}
	return writes
}

// parseCondInst 解析 if/while/for 括号内的条件，直接构造 *ast.Instruction。
// 调用时 peek() 为 LParen；返回后已消费 RParen。
func (p *parser) parseCondInst() *ast.Instruction {
	p.expect(LParen)
	inst := &ast.Instruction{}
	inst.Expr = p.parsePratt(0)
	p.expect(RParen)
	return inst
}

// ── 算子判断 ──────────────────────────────────────────────────

func isUnaryPrefixOp(s string) bool { return s == "!" || s == "-" }
