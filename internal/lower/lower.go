// Package lower 将结构化控制流 (if/while) lowering 为基本块原语 (br/goto)。
// for 循环 (路径迭代) 暂不 lowering，待执行层迭代原语就绪后再处理。
package lower

import (
	"fmt"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/parser"
)

// File 将文件中所有函数的控制流 lowering 为基本块。
func File(f *ast.File) *ast.File {
	lowered := &ast.File{
		TopLevelCalls: f.TopLevelCalls,
		PreambleLines: f.PreambleLines,
	}
	for _, fn := range f.Funcs {
		lf := Func(&fn)
		lowered.Funcs = append(lowered.Funcs, *lf)
	}
	return lowered
}

// Func 将函数体中 if/while 控制流 lowering 为 BlockStmt + br/goto。
func Func(fn *ast.Func) *ast.Func {
	lg := &labelGen{parent: fn.Name}
	lowered := &ast.Func{
		Name:      fn.Name,
		Signature: fn.Signature,
		Body:      lowerBody(fn.Body, lg),
	}
	return lowered
}

type labelGen struct {
	n      int
	parent string // 父函数名，用于构造 goto→call 的完整 label 路径
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

	thenLabel := lg.next("then")
	elseLabel := lg.next("else")
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

func evalCond(cond string, lg *labelGen) (insts []ast.Stmt, slot string) {
	cond = strings.TrimSpace(cond)
	if isSimpleRef(cond) {
		return nil, cond
	}
	slot = "./_cond_" + lg.next("cond")
	inst, err := parser.ParseLine(cond + " -> " + slot)
	if err != nil || inst == nil {
		return nil, cond
	}
	return []ast.Stmt{inst}, slot
}

func isSimpleRef(s string) bool {
	for _, c := range s {
		switch c {
		case ' ', '+', '-', '*', '/', '<', '>', '=', '!', '&', '|', '(', ')':
			return false
		}
	}
	return true
}

func brInst(cond, tLabel, fLabel string) *ast.Instruction {
	return &ast.Instruction{Opcode: "br", Reads: []string{cond, tLabel, fLabel}}
}

// gotoLabel — label 即无参 call，使用 parent/label 完整路径。
func gotoLabel(parent, label string) *ast.Instruction {
	return &ast.Instruction{Opcode: "call", Reads: []string{parent + "/" + label}}
}

func wrapBlock(label string, stmts []ast.Stmt, lg *labelGen) *ast.BlockStmt {
	if label == "" {
		label = lg.next("block")
	}
	return &ast.BlockStmt{Label: label, Body: stmts}
}
