// inst.go: 指令级解析——Pratt 解析器（S3：表达式优先级）。
//
// (p *parser).parseInst() / parseCondInst() 直接在 token 流上解析，
// 无中间 token buffer，无启发式停止条件。
package parser

import (
	"fmt"
	"strconv"

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
		// 优化：-<数字字面量> 直接合并为负数叶节点（如 -42 → Leaf("-42")），
		// 避免产生 Call("-", Leaf("42")) 的嵌套结构，符合读写码"参数为叶节点"约束。
		if t.Value == "-" {
			next := p.peek()
			if next.Kind == Literal && len(next.Value) > 0 && next.Value[0] >= '0' && next.Value[0] <= '9' {
				lit := p.advance()
				return ast.Leaf("-" + lit.Value)
			}
		}
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
	t = p.advance()
	// 引号字符串（非数字、非路径）：加 " 前缀编码，供 resolveReadValue 识别
	if t.Kind == Literal {
		v := t.Value
		isNum := isNumericLiteral(v)
		isPath := len(v) >= 2 && (v[:2] == "./" || v[0] == '/')
		if !isNum && !isPath {
			// 语法错误：以数字开头的 Literal 必然是数字字面量意图，
			// 但 isNumericLiteral 验证不通过（如 "1e"、"42e+"）。
			// 对标 Go/Rust：无效科学计数法字面量 → 编译错误。
			if len(v) > 0 && v[0] >= '0' && v[0] <= '9' {
				p.errors = append(p.errors, Diagnostic{
					Pos:     t.Pos,
					Message: fmt.Sprintf("invalid numeric literal %q", v),
				})
			}
			if t.Quote == 96 {
			return ast.RawStr(v)
		}
		return ast.StrLit(v)
		}
	}
	return ast.Leaf(t.Value)
}

// ── 写槽收集 ──────────────────────────────────────────────────

// collectWriteList 收集 -> 右侧的写槽列表。
// 支持 (a, b) 带括号形式和裸 a[, b...] 形式。
//
// 写槽只能是裸标识符（路径名），若遇到非标识符 token（如 LParen、Literal），
// 判定为同一行存在第二条指令，发出警告后停止收集。
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
		// 合法写槽：
		//   Ident  — 裸标识符（如 x, total, _）
		//   Literal 以 "./" 或 "/" 开头 — 路径引用（如 ./x, /abs/path）
		// 非法（触发 warning）：
		//   Ident 后紧跟 LParen — 是函数调用，说明同行存在第二条指令
		//   Literal 以 '"' 或数字开头 — 字符串/数字字面量，不能是写槽
		//   其他 token（LParen 等）
		isPathLiteral := t.Kind == Literal && len(t.Value) >= 2 &&
			(t.Value[:2] == "./" || t.Value[0] == '/')
		isIdent := t.Kind == Ident
		isCallStart := isIdent && p.peekAt(1).Kind == LParen
		isInvalidLiteral := t.Kind == Literal && !isPathLiteral

		switch {
		case isCallStart:
			p.errors = append(p.errors, Diagnostic{
				Pos:  t.Pos,
				Warn: true,
				Message: fmt.Sprintf(
					"function call %q on same line as write slot — "+
						"each instruction must be on its own line",
					t.Value),
			})
			return writes
		case isInvalidLiteral || (!isIdent && !isPathLiteral):
			p.errors = append(p.errors, Diagnostic{
				Pos:  t.Pos,
				Warn: true,
				Message: fmt.Sprintf(
					"unexpected token %q in write slot position — "+
						"did you put two instructions on the same line? "+
						"each instruction must be on its own line",
					t.Value),
			})
			return writes
		default:
			writes = append(writes, p.advance().Value)
		}
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

// isNumericLiteral 判断字符串是否为合法数字字面量。
//
// 对齐 Go strconv.ParseFloat 格式：接受 "42"、"3.14"、"1e10"、"1e+10"、"3.14e-5"。
// 首字符必为数字（scanner 保证），其余由 strconv.ParseFloat 权威校验。
//
// 拒绝："e"、"."、"-"、"1e"（无效科学计数）、"+42"（scanner 不产生此类 token）。
func isNumericLiteral(v string) bool {
	if len(v) == 0 {
		return false
	}
	if v[0] < '0' || v[0] > '9' {
		return false // 首字符非数字 → 不是数字字面量
	}
	_, err := strconv.ParseFloat(v, 64)
	return err == nil
}
