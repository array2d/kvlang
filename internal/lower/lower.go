// Package lower 将结构化控制流 (if/while) lowering 为基本块原语 (br/goto)。
//
// 设计原则（续体传递，Continuation-Passing Style）：
//   - if/while 遇到时，将后续所有语句作为续体（cont）传入，注入到 merge/exit block
//   - 函数入口始终在 [0,0]（平坦指令或 goto 首控制块）
//   - 任何路径最终以 return / goto / br 结尾（appendReturn 补全）
//   - break → goto exitLabel，continue → goto condLabel（由 loopCtx 携带当前循环出口）
//
// 读写码强制约束：
//   - 所有指令的参数必须为叶节点（路径/字面量），禁止内联嵌套表达式
//   - 违反者在 lower 阶段 panic（编译期错误），强制用户显式写槽
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

// loopCtx 携带当前最内层 while 循环的出口标签，供 break/continue 降级使用。
// nil 表示当前不在任何循环内。
type loopCtx struct {
	breakLabel    string // break  → goto breakLabel  (while 的 _exit_N)
	continueLabel string // continue → goto continueLabel (while 的 _while_N / condLabel)
}

// Func 将函数体中 if/while 控制流降级为 BlockStmt + br/goto，
// 展开复合表达式，并在函数尾部补充隐式 return（若缺失）。
func Func(fn *ast.Func) *ast.Func {
	lg := &labelGen{parent: fn.Sig.Name}
	body := lowerBody(fn.Body, lg, nil)
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
//   lc 携带最内层 while 的 break/continue 目标标签（函数顶层传 nil）。
func lowerBody(stmts []ast.Stmt, lg *labelGen, lc *loopCtx) []ast.Stmt {
	if len(stmts) == 0 {
		return nil
	}
	var preamble []ast.Stmt
	for i, st := range stmts {
		switch s := st.(type) {
		case *ast.Instruction:
			// 读写码原则：所有函数参数必须为叶节点（路径/字面量），禁止内联嵌套表达式。
			// 错误示例：print("r =", 10 - 3)   ← 10-3 是嵌套表达式，隐含"返回值"语义
			// 正确写法：10 - 3 -> r \n print("r =", r)
			if s.Expr != nil && !s.Expr.IsLeaf() && !allArgsLeaf(s.Expr) {
				panic(fmt.Sprintf(
					"kvlang read-write code: nested expression as argument is not allowed.\n"+
						"  got: %v\n"+
						"  fix: compute sub-expressions explicitly and assign to named slots first.",
					s.Expr,
				))
			}
			preamble = append(preamble, s)

		case *ast.IfStmt:
			cont := lowerBody(stmts[i+1:], lg, lc)
			return lowerIfWithCont(preamble, s, cont, lg, lc)

		case *ast.WhileStmt:
			cont := lowerBody(stmts[i+1:], lg, lc)
			return lowerWhileWithCont(preamble, s, cont, lg, lc)

		case *ast.BlockStmt:
			// 用户显式书写的基本块：递归降级体，保留原位。
			// 若前缀无终止符（preamble 为空或末尾非 return/goto/br），
			// 自动插入 goto firstBlock 确保 [0,0] 有指令（函数入口必须非空）。
			s.Body = lowerBody(s.Body, lg, lc)
			if !preambleEndsWithTerminator(preamble) {
				preamble = append(preamble, gotoLabel(lg.parent, s.Label))
			}
			out := append(preamble, ast.Stmt(s))
			return append(out, lowerBody(stmts[i+1:], lg, lc)...)

		case *ast.BreakStmt:
			// break → goto exitLabel（当前 while 的出口块）
			if lc != nil {
				preamble = append(preamble, gotoLabel(lg.parent, lc.breakLabel))
			}
			// break 是终止符：忽略其后的语句（不可达代码）
			return preamble

		case *ast.ContinueStmt:
			// continue → goto condLabel（当前 while 的条件块）
			if lc != nil {
				preamble = append(preamble, gotoLabel(lg.parent, lc.continueLabel))
			}
			// continue 是终止符：忽略其后的语句（不可达代码）
			return preamble

		case *ast.ForStmt:
			cont := lowerBody(stmts[i+1:], lg, lc)
			return lowerForWithCont(preamble, s, cont, lg, lc)

		default:
			// 未知节点 → 原样透传
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
//
// lc 透传给 then/else 体，确保嵌套 break/continue 仍指向外层 while。
func lowerIfWithCont(pre []ast.Stmt, s *ast.IfStmt, cont []ast.Stmt, lg *labelGen, lc *loopCtx) []ast.Stmt {
	condEval, condSlot := evalCond(s.Cond, lg)
	ifLabel    := lg.next("if")
	thenLabel  := lg.next("then")
	elseLabel  := lg.next("else")
	mergeLabel := lg.next("merge")

	condBody := append([]ast.Stmt{}, condEval...)
	// br 标签使用完整限定名（含父函数前缀），resolveLabel 直接返回，零 KV 查询
	condBody  = append(condBody, brInst(condSlot, lg.parent+"/"+thenLabel, lg.parent+"/"+elseLabel))

	// 将嵌套块从 then/else/cont 体内提升到函数顶层
	// lc 透传：if 内的 break/continue 仍指向外层 while
	thenInsts, thenBlocks := splitInstsAndBlocks(injectGoto(lowerBody(s.Then, lg, lc), lg.parent, mergeLabel))
	elseInsts, elseBlocks := splitInstsAndBlocks(injectGoto(lowerBody(s.Else, lg, lc), lg.parent, mergeLabel))
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
//
// 为 while 体创建新 loopCtx（break→exit, continue→while），
// cont（while 后的语句）继承外层 lc。
func lowerWhileWithCont(pre []ast.Stmt, s *ast.WhileStmt, cont []ast.Stmt, lg *labelGen, lc *loopCtx) []ast.Stmt {
	condEval, condSlot := evalCond(s.Cond, lg)
	condLabel := lg.next("while")
	bodyLabel := lg.next("do")
	exitLabel := lg.next("exit")

	condBody := append([]ast.Stmt{}, condEval...)
	// br 标签使用完整限定名（含父函数前缀），resolveLabel 直接返回，零 KV 查询
	condBody  = append(condBody, brInst(condSlot, lg.parent+"/"+bodyLabel, lg.parent+"/"+exitLabel))

	// 为 while 体构造新 loopCtx：break→exit, continue→condLabel
	bodyLc := &loopCtx{breakLabel: exitLabel, continueLabel: condLabel}
	bodyInsts, bodyBlocks := splitInstsAndBlocks(injectGoto(lowerBody(s.Body, lg, bodyLc), lg.parent, condLabel))
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

// lowerForWithCont 将 ForStmt 降级为四块结构（init/cond/body/exit），续体注入 exit block。
//
//	for (v in ./path) { body }
//
//	→ pre... goto _for_N
//	  _for_init_N:  { -1 -> ./_idx }
//	  _for_cond_N:  { ./_idx + 1 -> ./_idx;  kv.has(./path, ./_idx) -> ./_cond;
//	                   br(./_cond, fn/_for_body_N, fn/_for_exit_N) }
//	  _for_body_N:  { kv.at(./path, ./_idx) -> ./v;  lowerBody(body);
//	                   goto _for_cond_N }
//	  _for_exit_N:  { cont... }
//
// 为 for 体创建新 loopCtx（break→exit, continue→condLabel）。
func lowerForWithCont(pre []ast.Stmt, s *ast.ForStmt, cont []ast.Stmt, lg *labelGen, lc *loopCtx) []ast.Stmt {
	initLabel := lg.next("for_init")
	condLabel := lg.next("for_cond")
	bodyLabel := lg.next("for_body")
	exitLabel := lg.next("for_exit")

	// _for_init:  -1 -> ./_idx;  goto fn/_for_cond_N
	idxSlot := lg.tmp()
	condSlot := lg.tmp()
	initBody := []ast.Stmt{
		makeCopyInst("-1", idxSlot),
		gotoLabel(lg.parent, condLabel),
	}

	// _for_cond:  ./_idx + 1 -> ./_idx;  kv.has(./path, ./_idx) -> ./_cond;
	//             br(./_cond, fn/bodyLabel, fn/exitLabel)
	addInst := &ast.Instruction{
		Expr: ast.Call("+", ast.Leaf(idxSlot), ast.Leaf("1")),
		Writes: []string{idxSlot},
	}
	kvHasInst := &ast.Instruction{
		Expr: ast.Call("kv.has", ast.Leaf(s.Iter), ast.Leaf(idxSlot)),
		Writes: []string{condSlot},
	}
	brI := brInst(condSlot, lg.parent+"/"+bodyLabel, lg.parent+"/"+exitLabel)
	condBody := []ast.Stmt{addInst, kvHasInst, brI}

	// _for_body:  kv.at(./path, ./_idx) -> ./v;  lowerBody(body);  goto _for_cond_N
	kvAtInst := &ast.Instruction{
		Expr: ast.Call("kv.at", ast.Leaf(s.Iter), ast.Leaf(idxSlot)),
		Writes: []string{s.Var},
	}
	bodyLc := &loopCtx{breakLabel: exitLabel, continueLabel: condLabel}
	bodyInner := lowerBody(s.Body, lg, bodyLc)
	bodyInsts := append([]ast.Stmt{kvAtInst}, bodyInner...)
	bodyInsts = injectGoto(bodyInsts, lg.parent, condLabel)

	bodyInstsOnly, bodyBlocks := splitInstsAndBlocks(bodyInsts)
	injectGotoBlocks(bodyBlocks, lg.parent, condLabel)
	contInsts, contBlocks := splitInstsAndBlocks(cont)

	entry := append(pre, gotoLabel(lg.parent, initLabel))
	result := append(entry,
		&ast.BlockStmt{Label: initLabel, Body: initBody},
		&ast.BlockStmt{Label: condLabel, Body: condBody},
		&ast.BlockStmt{Label: bodyLabel, Body: bodyInstsOnly},
		&ast.BlockStmt{Label: exitLabel, Body: contInsts},
	)
	result = append(result, bodyBlocks...)
	result = append(result, contBlocks...)
	return result
}

// makeCopyInst 创建值复制指令：val -> ./dest
func makeCopyInst(val, dest string) *ast.Instruction {
	return &ast.Instruction{
		Expr:   ast.Leaf(val),
		Writes: []string{dest},
	}
}

// // splitInstsAndBlocks 将语句列表分为非块语句（指令/goto/return）和块语句两组，
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
//   - 简单槽引用（叶节点，无 Reads）→ 直接用 Val 作为槽名（如 while (hit)）
//   - 简单比较（所有参数为叶节点）→ 写入临时槽 _N（如 while (i <= n)）
//   - 嵌套表达式（如 add(a,b) > 0）→ 编译期 panic：先显式求值再用槽
func evalCond(cond *ast.Instruction, lg *labelGen) (insts []ast.Stmt, slot string) {
	if isCondSimpleSlot(cond) {
		return nil, cond.Expr.Val
	}
	if !allArgsLeaf(cond.Expr) {
		panic(fmt.Sprintf(
			"kvlang read-write code: nested expression in condition is not allowed.\n"+
				"  got: %v\n"+
				"  fix: compute sub-expression first and assign to a slot, e.g.:\n"+
				"       add(a, b) -> _cond_val\n"+
				"       while (_cond_val > 0) { ... }",
			cond.Expr,
		))
	}
	slot = lg.tmp()
	condInst := *cond
	condInst.Writes = []string{slot}
	insts = append(insts, &condInst)
	return insts, slot
}

func isCondSimpleSlot(inst *ast.Instruction) bool {
	return inst.Expr != nil && inst.Expr.IsLeaf() && len(inst.Writes) == 0
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
		args[i] = ast.Leaf(p.Name) // 裸标识符，与 ./p.Name 等价
	}
	return &ast.Instruction{Expr: ast.Call(op.OpReturn, args...)}
}
