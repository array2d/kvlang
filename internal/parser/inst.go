// inst.go: 指令级解析。
// (p *parser).parseInst() 直接在 token 流上解析一条 *ast.Instruction，无中间 buffer。
package parser

import (
	"strings"

	"kvlang/internal/ast"
)

// parseInst 直接在 token 流上解析一条指令，纯递归下降，无中间 token buffer。
// 停止于 Newline / RBrace / EOF（自然语句边界）。
func (p *parser) parseInst() *ast.Instruction {
	inst := &ast.Instruction{}

	// 前瞻：找到第一个顶层 Arrow（不在括号内）
	arrowAbs, arrowVal := p.findTopLevelArrow()

	switch {
	case arrowAbs >= 0 && arrowVal == "<-":
		// (writes) <- expr
		inst.Writes = p.collectWritesUntilArrow()
		p.advance() // consume <-
		p.parseExprInto(inst)
	case arrowAbs >= 0:
		// expr -> (writes)
		p.parseExprInto(inst)
		p.advance() // consume ->
		inst.Writes = p.collectWriteList()
	default:
		// 无 Arrow：纯表达式
		p.parseExprInto(inst)
	}

	p.eat(Newline)
	return inst
}

// findTopLevelArrow 前瞻（不消费）找第一个深度为 0 的 Arrow token 绝对下标。
// 遇 Newline / RBrace / EOF 停止，返回 (-1, "") 表示未找到。
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
		case Newline, RBrace, EOF:
			return -1, ""
		}
	}
	return -1, ""
}

// parseExprInto 解析表达式部分，写入 inst.Opcode 和 inst.Reads。
// 停止于 Arrow / RParen / Newline / RBrace / EOF。
func (p *parser) parseExprInto(inst *ast.Instruction) {
	t := p.peek()
	if t.Kind == Arrow || t.Kind == RParen || t.Kind == Newline || t.Kind == RBrace || t.Kind == EOF {
		return
	}

	// 3a. 前缀调用：name(args...)  — 含 return(x), br(c,t,f) 等
	if p.peekAt(1).Kind == LParen {
		inst.Opcode = p.advance().Value // consume name/keyword
		p.advance()                     // consume (
		inst.Reads = p.collectArgList()
		p.expect(RParen)
		return
	}

	// 3b. 中缀算子：A op B
	next := p.peekAt(1)
	if next.Kind != EOF && next.Kind != Newline && next.Kind != Arrow &&
		next.Kind != RBrace && next.Kind != RParen && isInfixOp(next.Value) {
		inst.Reads = append(inst.Reads, p.advance().Value) // A
		inst.Opcode = p.advance().Value                    // op
		// 右操作数：收集到下一个边界
		var sb strings.Builder
		for {
			t2 := p.peek()
			if t2.Kind == Arrow || t2.Kind == RParen || t2.Kind == Newline ||
				t2.Kind == RBrace || t2.Kind == EOF {
				break
			}
			sb.WriteString(p.advance().Value)
		}
		if sb.Len() > 0 {
			inst.Reads = append(inst.Reads, sb.String())
		}
		return
	}

	// 3c. 一元前缀算子：!A 或 -A
	if isUnaryPrefixOp(t.Value) {
		next2 := p.peekAt(1)
		if next2.Kind != EOF && next2.Kind != Newline && next2.Kind != Arrow && next2.Kind != RParen {
			inst.Opcode = p.advance().Value
			inst.Reads = append(inst.Reads, p.advance().Value)
			return
		}
	}

	// 3d. 裸操作码 / 槽引用
	inst.Opcode = p.advance().Value
}

// collectArgList 收集函数调用括号内的参数列表，遇 RParen 或 EOF 停止（不消费 RParen）。
func (p *parser) collectArgList() []string {
	var args []string
	for p.peek().Kind != RParen && p.peek().Kind != EOF {
		if p.eat(Comma) {
			continue
		}
		args = append(args, p.advance().Value)
	}
	return args
}

// collectWriteList 收集 -> 右侧的写槽列表。
// 支持 (a, b) 带括号形式和裸 a, b 形式。
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
		if t.Kind == Newline || t.Kind == RBrace || t.Kind == EOF || t.Kind == RParen {
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

// collectWritesUntilArrow 收集 <- 左侧的写槽，直到遇到 Arrow 停止（不消费 Arrow）。
func (p *parser) collectWritesUntilArrow() []string {
	hasParen := p.peek().Kind == LParen
	if hasParen {
		p.advance() // consume (
	}
	var writes []string
	for {
		t := p.peek()
		if t.Kind == Arrow || t.Kind == EOF {
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

// ── 算子判断 ──────────────────────────────────────────────────

var infixOpSet = map[string]bool{
	"+": true, "-": true, "*": true, "/": true, "%": true,
	"==": true, "!=": true, "<": true, ">": true, "<=": true, ">=": true,
	"&&": true, "||": true, "&": true, "|": true, "^": true, "<<": true, ">>": true,
}

func isInfixOp(s string) bool       { return infixOpSet[s] }
func isUnaryPrefixOp(s string) bool { return s == "!" || s == "-" }
