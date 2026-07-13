// Package lower 将结构化控制流 (if/while) lowering 为基本块原语 (br/goto)。
// for 循环 (路径迭代) 暂不 lowering，待执行层迭代原语就绪后再处理。
package lower

import (
	"fmt"
	"kvlang/internal/ast"
)

// File 将文件中所有函数的控制流 lowering 为基本块。
func File(f *ast.File) *ast.File {
	lowered := &ast.File{
		TopLevelCalls: f.TopLevelCalls,
	}
	for _, fn := range f.Funcs {
		lf := Func(&fn)
		lowered.Funcs = append(lowered.Funcs, *lf)
	}
	return lowered
}

// Func 将函数体中 if/while 控制流 lowering 为 BlockStmt + br/goto。
func Func(fn *ast.Func) *ast.Func {
	lg := &labelGen{parent: fn.Sig.Name}
	return &ast.Func{
		Sig:  fn.Sig,
		Body: lowerBody(fn.Body, lg),
	}
}

type labelGen struct {
	n      int
	parent string
}

func (g *labelGen) next(prefix string) string {
	g.n++
	return fmt.Sprintf("_%s_%d", prefix, g.n)
}

// lowerBody 将语句列表中的 if/while 转换为基本块，for 保持原样。
func lowerBody(stmts []ast.Stmt, lg *labelGen) []ast.Stmt {
	var result []ast.Stmt
	var pending []ast.Stmt

	for _, st := range stmts {
		switch s := st.(type) {
		case *ast.Instruction:
			pending = append(pending, s)

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
			// for / break / continue 保持原样
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

	thenLabel := "iftrue"
	elseLabel := "iffalse"
	mergeLabel := lg.next("merge")

	condBody := append([]ast.Stmt{}, condEval...)
	condBody = append(condBody, brInst(condSlot, thenLabel, elseLabel))

	thenBody := append(lowerBody(s.Then, lg), gotoLabel(lg.parent, mergeLabel))
	elseBody := append(lowerBody(s.Else, lg), gotoLabel(lg.parent, mergeLabel))

	return []ast.Stmt{
		&ast.BlockStmt{Label: lg.next("if_cond"), Body: condBody},
		&ast.BlockStmt{Label: thenLabel, Body: thenBody},
		&ast.BlockStmt{Label: elseLabel, Body: elseBody},
		&ast.BlockStmt{Label: mergeLabel, Body: nil},
	}
}

func lowerWhile(s *ast.WhileStmt, lg *labelGen) []ast.Stmt {
	condEval, condSlot := evalCond(s.Cond, lg)

	condLabel := lg.next("while_cond")
	bodyLabel := lg.next("while_body")
	exitLabel := lg.next("while_exit")

	condBody := append([]ast.Stmt{}, condEval...)
	condBody = append(condBody, brInst(condSlot, bodyLabel, exitLabel))

	bodyStmts := lowerBody(s.Body, lg)
	bodyStmts = append(bodyStmts, gotoLabel(lg.parent, condLabel))

	return []ast.Stmt{
		&ast.BlockStmt{Label: condLabel, Body: condBody},
		&ast.BlockStmt{Label: bodyLabel, Body: bodyStmts},
		&ast.BlockStmt{Label: exitLabel, Body: nil},
	}
}

// evalCond 处理条件指令：
//   - 简单槽引用（无 Reads）→ 直接用 Opcode 作为槽名
//   - 复合表达式 → 生成临时槽，填写 Writes
func evalCond(cond *ast.Instruction, lg *labelGen) (insts []ast.Stmt, slot string) {
	if isCondSimpleSlot(cond) {
		return nil, cond.Opcode
	}
	slot = "./_cond_" + lg.next("cond")
	condInst := *cond // 浅拷贝，不修改原始 AST 节点
	condInst.Writes = []string{slot}
	return []ast.Stmt{&condInst}, slot
}

// isCondSimpleSlot 判断条件是否为裸槽引用（无 Reads，无 Writes）。
// 裸槽引用直接作为 br 的条件操作数，无需生成中间指令。
func isCondSimpleSlot(inst *ast.Instruction) bool {
	return len(inst.Reads) == 0 && len(inst.Writes) == 0
}

func brInst(cond, tLabel, fLabel string) *ast.Instruction {
	return &ast.Instruction{Opcode: "br", Reads: []string{cond, tLabel, fLabel}}
}

func gotoLabel(parent, label string) *ast.Instruction {
	return &ast.Instruction{Opcode: "call", Reads: []string{parent + "/" + label}}
}

func wrapBlock(label string, stmts []ast.Stmt, lg *labelGen) *ast.BlockStmt {
	if label == "" {
		label = lg.next("block")
	}
	return &ast.BlockStmt{Label: label, Body: stmts}
}

