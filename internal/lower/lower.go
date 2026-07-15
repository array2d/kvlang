// Package lower 将结构化控制流 (if/while) lowering 为基本块原语 (br/goto)，
// 并将复合表达式展开为 kvcpu 可直接执行的平坦指令序列。
// for / break / continue 暂不处理，待执行层迭代原语就绪后再处理（见 todo.md P11）。
//
// 设计原则（续体传递，Continuation-Passing Style）：
//   - if/while 遇到时，将后续所有语句作为续体（cont）传入，注入到 merge/exit block
//   - 函数入口始终在 [0,0]（平坦指令或 goto 首控制块）
//   - 任何路径最终以 return / goto / br 结尾（appendReturn 补全）
package lower

import (
	"fmt"
	"kvlang/internal/ast"
	"kvlang/internal/op"
)

// File 将文件中所有函数降级。
func File(f *ast.File) *ast.File {
	lowered := &ast.File{TopLevelCalls: f.TopLevelCalls}
	for _, fn := range f.Funcs {
		lf := Func(&fn)
		lowered.Funcs = append(lowered.Funcs, *lf)
	}
	return lowered
}

// Func 将函数体中 if/while 控制流降级为 BlockStmt + br/goto，
// 展开复合表达式，并在函数尾部补充隐式 return（若缺失）。
func Func(fn *ast.Func) *ast.Func {
	lg := &labelGen{parent: fn.Sig.Name}
	body := lowerBody(fn.Body, lg)
	body = appendReturn(body, fn.Sig)
	return &ast.Func{Sig: fn.Sig, Body: body}
}

// labelGen 为同一函数内的所有编译器产物生成唯一名称。
type labelGen struct {
	n      int
	parent string
}

// next 生成带语义前缀的块标签，格式 _prefix_N（如 _then_1, _merge_2）。
func (g *labelGen) next(prefix string) string {
	g.n++
	return fmt.Sprintf("_%s_%d", prefix, g.n)
}

// tmp 生成匿名中间变量槽名，格式 _N（如 _3, _4）。
func (g *labelGen) tmp() string {
	g.n++
	return fmt.Sprintf("_%d", g.n)
}

// lowerBody 以续体传递风格将语句列表降级：
//   遇到 if/while 时将后续语句作为续体注入 merge/exit block，
//   保证函数入口 [0,0] 始终有指令（平坦指令或 goto 首控制块）。
func lowerBody(stmts []ast.Stmt, lg *labelGen) []ast.Stmt {
	if len(stmts) == 0 {
		return nil
	}
	var preamble []ast.Stmt
	for i, st := range stmts {
		switch s := st.(type) {
		case *ast.Instruction:
			preamble = append(preamble, flattenInstExpr(s, lg)...)

		case *ast.IfStmt:
			cont := lowerBody(stmts[i+1:], lg)
			return lowerIfWithCont(preamble, s, cont, lg)

		case *ast.WhileStmt:
			cont := lowerBody(stmts[i+1:], lg)
			return lowerWhileWithCont(preamble, s, cont, lg)

		case *ast.BlockStmt:
			// 用户显式书写的基本块：递归降级体，保留原位。
			// 若前缀无终止符（preamble 为空或末尾非 return/goto/br），
			// 自动插入 goto firstBlock 确保 [0,0] 有指令（函数入口必须非空）。
			s.Body = lowerBody(s.Body, lg)
			if !preambleEndsWithTerminator(preamble) {
				preamble = append(preamble, gotoLabel(lg.parent, s.Label))
			}
			out := append(preamble, ast.Stmt(s))
			return append(out, lowerBody(stmts[i+1:], lg)...)

		default:
			// for / break / continue 保持原样（P11）
			preamble = append(preamble, s)
		}
	}
	// 全程无控制流：直接返回平坦指令
	return preamble
}

// lowerIfWithCont 将 IfStmt 降级为四块结构，续体注入 merge block。
// 所有内层块（嵌套 if/while 产生的 BlockStmt）从 then/else/cont 体内提升到函数顶层，
// 确保所有块均为兄弟节点（读写码结构），与 RegisterBlocks 保持一致。
//
//	pre... goto _if_N
//	_if_N:   { evalCond; br(cond, fn/_then_N, fn/_else_N) }
//	_then_N: { lowerBody(Then)[insts only]; goto _merge_N }
//	_else_N: { lowerBody(Else)[insts only]; goto _merge_N }
//	_merge_N:{ cont[insts only] }
//	[promoted inner blocks from then/else/cont...]
func lowerIfWithCont(pre []ast.Stmt, s *ast.IfStmt, cont []ast.Stmt, lg *labelGen) []ast.Stmt {
	condEval, condSlot := evalCond(s.Cond, lg)
	ifLabel    := lg.next("if")
	thenLabel  := lg.next("then")
	elseLabel  := lg.next("else")
	mergeLabel := lg.next("merge")

	condBody := append([]ast.Stmt{}, condEval...)
	// br 标签使用完整限定名（含父函数前缀），resolveLabel 直接返回，零 KV 查询
	condBody  = append(condBody, brInst(condSlot, lg.parent+"/"+thenLabel, lg.parent+"/"+elseLabel))

	// 将嵌套块从 then/else/cont 体内提升到函数顶层
	thenInsts, thenBlocks := splitInstsAndBlocks(injectGoto(lowerBody(s.Then, lg), lg.parent, mergeLabel))
	elseInsts, elseBlocks := splitInstsAndBlocks(injectGoto(lowerBody(s.Else, lg), lg.parent, mergeLabel))
	contInsts, contBlocks := splitInstsAndBlocks(cont)

	// 修复：提升出来的内层块（如嵌套 if 的 merge 块）也需要正确跳到当前 mergeLabel。
	// injectGoto 只处理最后一个块；中间无终止符的块（如嵌套 else 的空 merge）需要补全。
	injectGotoBlocks(thenBlocks, lg.parent, mergeLabel)
	injectGotoBlocks(elseBlocks, lg.parent, mergeLabel)

	entry := append(pre, gotoLabel(lg.parent, ifLabel))
	result := append(entry,
		&ast.BlockStmt{Label: ifLabel,    Body: condBody},
		&ast.BlockStmt{Label: thenLabel,  Body: thenInsts},
		&ast.BlockStmt{Label: elseLabel,  Body: elseInsts},
		&ast.BlockStmt{Label: mergeLabel, Body: contInsts},
	)
	// 提升内层块为函数顶层兄弟节点
	result = append(result, thenBlocks...)
	result = append(result, elseBlocks...)
	result = append(result, contBlocks...)
	return result
}

// lowerWhileWithCont 将 WhileStmt 降级为三块结构，续体注入 exit block。
// 同 lowerIfWithCont，内层块从 body/cont 中提升到函数顶层（读写码结构）。
//
//	pre... goto _while_N
//	_while_N: { evalCond; br(cond, fn/_do_N, fn/_exit_N) }
//	_do_N:    { lowerBody(Body)[insts only]; goto _while_N }
//	_exit_N:  { cont[insts only] }
//	[promoted inner blocks...]
func lowerWhileWithCont(pre []ast.Stmt, s *ast.WhileStmt, cont []ast.Stmt, lg *labelGen) []ast.Stmt {
	condEval, condSlot := evalCond(s.Cond, lg)
	condLabel := lg.next("while")
	bodyLabel := lg.next("do")
	exitLabel := lg.next("exit")

	condBody := append([]ast.Stmt{}, condEval...)
	// br 标签使用完整限定名（含父函数前缀），resolveLabel 直接返回，零 KV 查询
	condBody  = append(condBody, brInst(condSlot, lg.parent+"/"+bodyLabel, lg.parent+"/"+exitLabel))

	bodyInsts, bodyBlocks := splitInstsAndBlocks(injectGoto(lowerBody(s.Body, lg), lg.parent, condLabel))
	// 修复：提升出来的所有内层块也需要跳回 while 条件块（lowerIfWithCont 已为其注入 goto 合并块，
	// 但最外层合并块本身仍可能缺少 goto _while_N）。
	injectGotoBlocks(bodyBlocks, lg.parent, condLabel)
	contInsts, contBlocks := splitInstsAndBlocks(cont)

	entry := append(pre, gotoLabel(lg.parent, condLabel))
	result := append(entry,
		&ast.BlockStmt{Label: condLabel, Body: condBody},
		&ast.BlockStmt{Label: bodyLabel, Body: bodyInsts},
		&ast.BlockStmt{Label: exitLabel, Body: contInsts},
	)
	result = append(result, bodyBlocks...)
	result = append(result, contBlocks...)
	return result
}

// splitInstsAndBlocks 将语句列表分为非块语句（指令/goto/return）和块语句两组，
// 用于将嵌套块从 then/else/body/cont 中提升到函数顶层（读写码结构）。
func splitInstsAndBlocks(stmts []ast.Stmt) (insts, blocks []ast.Stmt) {
	for _, s := range stmts {
		if _, ok := s.(*ast.BlockStmt); ok {
			blocks = append(blocks, s)
		} else {
			insts = append(insts, s)
		}
	}
	return
}

// injectGotoBlocks 对切片中每个 BlockStmt 的 body 逐一调用 injectGoto。
// 仅在块无终止符时才注入，已有终止符的块不受影响。
// 用于修复提升出来的内层块（如深层嵌套 if 的 merge 块）缺少跳转的问题。
func injectGotoBlocks(stmts []ast.Stmt, parent, label string) {
	for _, s := range stmts {
		if b, ok := s.(*ast.BlockStmt); ok {
			b.Body = injectGoto(b.Body, parent, label)
		}
	}
}

// injectGoto 在 body 的最后非终止点追加 goto 指令。
// 若末尾为控制转移（return/goto/br），则不追加。
// 若末尾为 BlockStmt，则递归注入到该块的尾部。
func injectGoto(body []ast.Stmt, parent, label string) []ast.Stmt {
	g := gotoLabel(parent, label)
	if len(body) == 0 {
		return []ast.Stmt{g}
	}
	switch s := body[len(body)-1].(type) {
	case *ast.Instruction:
		if isTerminator(s) {
			return body
		}
		return append(body, g)
	case *ast.BlockStmt:
		s.Body = injectGoto(s.Body, parent, label)
		return body
	}
	return append(body, g)
}

// isTerminator 判断指令是否为控制转移（执行后不继续顺序执行）。
func isTerminator(s *ast.Instruction) bool {
	if s.Expr == nil {
		return false
	}
	switch s.Expr.Op {
	case op.OpReturn, op.OpGoto, op.OpBr:
		return true
	}
	return false
}

// evalCond 处理条件指令：
//   - 简单槽引用（叶节点，无 Reads）→ 直接用 Val 作为槽名
//   - 复合表达式 → 展开并写入临时槽 _N
func evalCond(cond *ast.Instruction, lg *labelGen) (insts []ast.Stmt, slot string) {
	if isCondSimpleSlot(cond) {
		return nil, cond.Expr.Val
	}
	slot = lg.tmp()
	condInst := *cond
	condInst.Writes = []string{slot}
	for _, fi := range flattenInstExpr(&condInst, lg) {
		insts = append(insts, fi)
	}
	return insts, slot
}

func isCondSimpleSlot(inst *ast.Instruction) bool {
	return inst.Expr != nil && inst.Expr.IsLeaf() && len(inst.Writes) == 0
}

// flattenInstExpr 将复合子表达式展开为平坦指令序列（kvcpu 可直接执行）。
func flattenInstExpr(inst *ast.Instruction, lg *labelGen) []ast.Stmt {
	if inst.Expr == nil || inst.Expr.IsLeaf() || allArgsLeaf(inst.Expr) {
		return []ast.Stmt{inst}
	}
	var result []ast.Stmt
	flatArgs := make([]*ast.Expr, len(inst.Expr.Args))
	for i, arg := range inst.Expr.Args {
		if arg == nil || arg.IsLeaf() {
			flatArgs[i] = arg
		} else {
			tmp := lg.tmp()
			subInst := &ast.Instruction{Expr: arg, Writes: []string{tmp}}
			result = append(result, flattenInstExpr(subInst, lg)...)
			flatArgs[i] = ast.Leaf(tmp)
		}
	}
	result = append(result, &ast.Instruction{
		Expr:   ast.Call(inst.Expr.Op, flatArgs...),
		Writes: inst.Writes,
	})
	return result
}

// preambleEndsWithTerminator 判断 preamble 最后一条指令是否为终止符。
// 用于决定是否在遇到用户块之前自动插入 goto。
func preambleEndsWithTerminator(preamble []ast.Stmt) bool {
	if len(preamble) == 0 {
		return false
	}
	if inst, ok := preamble[len(preamble)-1].(*ast.Instruction); ok {
		return isTerminator(inst)
	}
	return false
}

func allArgsLeaf(e *ast.Expr) bool {
	for _, a := range e.Args {
		if a != nil && !a.IsLeaf() {
			return false
		}
	}
	return true
}

func brInst(cond, tLabel, fLabel string) *ast.Instruction {
	return &ast.Instruction{
		Expr: ast.Call(op.OpBr, ast.Leaf(cond), ast.Leaf(tLabel), ast.Leaf(fLabel)),
	}
}

func gotoLabel(parent, label string) *ast.Instruction {
	return &ast.Instruction{
		Expr: ast.Call(op.OpGoto, ast.Leaf(parent+"/"+label)),
	}
}

// appendReturn 递归为所有块补充隐式 return。
//
// 读写码结构下函数体中每个块均可能是出口块（如 if-merge、while-exit），
// 必须对 ALL BlockStmt 递归注入，而非只处理最后一个块。
//
// 规则：
//   - 块体为空 → 追加 return(retVals...)
//   - 末尾为裸 return（无参数）且函数有返回值 → 替换为 return(./r0, ...)
//   - 末尾为非终止符指令 → 追加 return(retVals...)
//   - 末尾为完整终止符（goto/br/return 带参） → 不处理
func appendReturn(body []ast.Stmt, sig ast.FuncSig) []ast.Stmt {
	if len(body) == 0 {
		return []ast.Stmt{makeReturnInst(sig)}
	}
	// 对函数体内所有 BlockStmt 递归注入（读写码中每块都可能是出口）
	for _, s := range body {
		if b, ok := s.(*ast.BlockStmt); ok {
			b.Body = appendReturn(b.Body, sig)
		}
	}
	// 处理函数体/块体本身的末尾
	last, ok := body[len(body)-1].(*ast.Instruction)
	if !ok {
		return body // BlockStmt 已递归处理
	}
	if isBareReturn(last) && len(sig.Returns) > 0 {
		// 裸 return（用户写的无参 return）→ 补全为 return(./retval...)
		body[len(body)-1] = makeReturnInst(sig)
		return body
	}
	if !isTerminator(last) {
		return append(body, makeReturnInst(sig))
	}
	return body
}

// isBareReturn 判断是否为无参数的 return 指令（用户书写裸 return 关键字时产生）。
func isBareReturn(s *ast.Instruction) bool {
	if s.Expr == nil {
		return false
	}
	opcode, reads := s.Flat()
	return opcode == op.OpReturn && len(reads) == 0
}

func makeReturnInst(sig ast.FuncSig) *ast.Instruction {
	args := make([]*ast.Expr, len(sig.Returns))
	for i, p := range sig.Returns {
		args[i] = ast.Leaf("./" + p.Name)
	}
	return &ast.Instruction{Expr: ast.Call(op.OpReturn, args...)}
}
