// Package lower 将结构化控制流 (if/while) lowering 为基本块原语 (br/call)，
// 并将复合表达式展开为 kvcpu 可直接执行的平坦指令序列。
// for / break / continue 暂不处理，待执行层迭代原语就绪后再处理（见 todo.md P11）。
package lower

import (
	"fmt"
	"kvlang/internal/ast"
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

// Func 将函数体中 if/while 控制流降级为 BlockStmt + br/call，
// 并将复合表达式展开为平坦指令序列。
func Func(fn *ast.Func) *ast.Func {
	lg := &labelGen{parent: fn.Sig.Name}
	return &ast.Func{
		Sig:  fn.Sig,
		Body: lowerBody(fn.Body, lg),
	}
}

// labelGen 为同一函数内的所有编译器产物生成唯一名称。
// 单调递增计数器在块标签（next）和临时槽（tmp）之间共享，保证全函数唯一。
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
// _ 开头不在用户标识符字符集 [a-z0-9-] 中，不会与用户变量冲突。
func (g *labelGen) tmp() string {
	g.n++
	return fmt.Sprintf("_%d", g.n)
}

func lowerBody(stmts []ast.Stmt, lg *labelGen) []ast.Stmt {
	var result []ast.Stmt
	var pending []ast.Stmt

	for _, st := range stmts {
		switch s := st.(type) {
		case *ast.Instruction:
			pending = append(pending, flattenInstExpr(s, lg)...)

		case *ast.BlockStmt:
			s.Body = lowerBody(s.Body, lg)
			if len(pending) > 0 {
				result = append(result, wrapBlock("", pending, lg))
				pending = nil
			}
			result = append(result, s)

		case *ast.IfStmt:
			blocks := lowerIf(s, lg)
			if len(pending) > 0 {
				pending = append(pending, gotoLabel(lg.parent, blocks[0].(*ast.BlockStmt).Label))
				result = append(result, wrapBlock("", pending, lg))
				pending = nil
			}
			result = append(result, blocks...)

		case *ast.WhileStmt:
			blocks := lowerWhile(s, lg)
			if len(pending) > 0 {
				pending = append(pending, gotoLabel(lg.parent, blocks[0].(*ast.BlockStmt).Label))
				result = append(result, wrapBlock("", pending, lg))
				pending = nil
			}
			result = append(result, blocks...)

		default:
			// for / break / continue 保持原样（P11）
			pending = append(pending, s)
		}
	}

	if len(pending) > 0 {
		result = append(result, wrapBlock("", pending, lg))
	}
	return result
}

func lowerIf(s *ast.IfStmt, lg *labelGen) []ast.Stmt {
	condEval, condSlot := evalCond(s.Cond, lg)

	thenLabel  := lg.next("then")
	elseLabel  := lg.next("else")
	mergeLabel := lg.next("merge")

	condBody := append([]ast.Stmt{}, condEval...)
	condBody  = append(condBody, brInst(condSlot, thenLabel, elseLabel))

	thenBody := append(lowerBody(s.Then, lg), gotoLabel(lg.parent, mergeLabel))
	elseBody := append(lowerBody(s.Else, lg), gotoLabel(lg.parent, mergeLabel))

	return []ast.Stmt{
		&ast.BlockStmt{Label: lg.next("if"), Body: condBody},
		&ast.BlockStmt{Label: thenLabel, Body: thenBody},
		&ast.BlockStmt{Label: elseLabel, Body: elseBody},
		&ast.BlockStmt{Label: mergeLabel, Body: nil},
	}
}

func lowerWhile(s *ast.WhileStmt, lg *labelGen) []ast.Stmt {
	condEval, condSlot := evalCond(s.Cond, lg)

	condLabel := lg.next("while")
	bodyLabel := lg.next("do")
	exitLabel := lg.next("exit")

	condBody := append([]ast.Stmt{}, condEval...)
	condBody  = append(condBody, brInst(condSlot, bodyLabel, exitLabel))

	bodyStmts := append(lowerBody(s.Body, lg), gotoLabel(lg.parent, condLabel))

	return []ast.Stmt{
		&ast.BlockStmt{Label: condLabel, Body: condBody},
		&ast.BlockStmt{Label: bodyLabel, Body: bodyStmts},
		&ast.BlockStmt{Label: exitLabel, Body: nil},
	}
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
// 每个中间结果写入临时槽 _N；若所有 Args 已为叶节点则直接返回原指令。
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
		Expr: ast.Call("br", ast.Leaf(cond), ast.Leaf(tLabel), ast.Leaf(fLabel)),
	}
}

func gotoLabel(parent, label string) *ast.Instruction {
	return &ast.Instruction{
		Expr: ast.Call("call", ast.Leaf(parent+"/"+label)),
	}
}

func wrapBlock(label string, stmts []ast.Stmt, lg *labelGen) *ast.BlockStmt {
	if label == "" {
		label = lg.next("block")
	}
	return &ast.BlockStmt{Label: label, Body: stmts}
}
