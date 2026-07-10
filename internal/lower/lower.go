// Package lower 将结构化控制流 (if/for/while) lowering 为基本块原语 (br/jump)。
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

// Func 将函数体中所有结构化控制流 lowering 为 BlockStmt + br/jump。
// 返回新的函数体（纯基本块序列）。
func Func(fn *ast.Func) *ast.Func {
	lowered := &ast.Func{
		Name:      fn.Name,
		Signature: fn.Signature,
		Body:      lowerBody(fn.Body, &labelGen{}),
	}
	return lowered
}

type labelGen struct {
	n int
}

func (g *labelGen) next(prefix string) string {
	g.n++
	return fmt.Sprintf("_%s_%d", prefix, g.n)
}

// lowerBody 将语句列表转换为纯 BlockStmt 序列。
func lowerBody(stmts []ast.Stmt, lg *labelGen) []ast.Stmt {
	var result []ast.Stmt
	var prevBlocks []ast.Stmt

	for _, st := range stmts {
		switch s := st.(type) {
		case *ast.Instruction:
			prevBlocks = append(prevBlocks, s)

		case *ast.BlockStmt:
			s.Body = lowerBody(s.Body, lg)
			if len(prevBlocks) > 0 {
				result = append(result, wrapBlock("", prevBlocks, lg))
				prevBlocks = nil
			}
			result = append(result, s)

		case *ast.IfStmt:
			blocks := lowerIf(s, lg)
			if len(prevBlocks) > 0 {
				prevBlocks = append(prevBlocks, jumpTo(blocks[0].(*ast.BlockStmt).Label))
				result = append(result, wrapBlock("", prevBlocks, lg))
				prevBlocks = nil
			}
			result = append(result, blocks...)

		case *ast.ForStmt:
			blocks := lowerFor(s, lg)
			if len(prevBlocks) > 0 {
				prevBlocks = append(prevBlocks, jumpTo(blocks[0].(*ast.BlockStmt).Label))
				result = append(result, wrapBlock("", prevBlocks, lg))
				prevBlocks = nil
			}
			result = append(result, blocks...)

		case *ast.WhileStmt:
			blocks := lowerWhile(s, lg)
			if len(prevBlocks) > 0 {
				prevBlocks = append(prevBlocks, jumpTo(blocks[0].(*ast.BlockStmt).Label))
				result = append(result, wrapBlock("", prevBlocks, lg))
				prevBlocks = nil
			}
			result = append(result, blocks...)

		case *ast.BreakStmt, *ast.ContinueStmt:
			prevBlocks = append(prevBlocks, s)
		}
	}

	if len(prevBlocks) > 0 {
		result = append(result, wrapBlock("", prevBlocks, lg))
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

	thenBody := append(lowerBody(s.Then, lg), jumpTo(mergeLabel))
	elseBody := append(lowerBody(s.Else, lg), jumpTo(mergeLabel))

	return []ast.Stmt{
		&ast.BlockStmt{Label: lg.next("if_cond"), Body: condBody},
		&ast.BlockStmt{Label: thenLabel, Body: thenBody},
		&ast.BlockStmt{Label: elseLabel, Body: elseBody},
		&ast.BlockStmt{Label: mergeLabel, Body: nil},
	}
}

func lowerFor(s *ast.ForStmt, lg *labelGen) []ast.Stmt {
	initLabel := lg.next("for_init")
	condLabel := lg.next("for_cond")
	bodyLabel := lg.next("for_body")
	stepLabel := lg.next("for_step")
	exitLabel := lg.next("for_exit")

	initBody := []ast.Stmt{
		strSetInst(s.Start, "./"+s.Var),
		jumpTo(condLabel),
	}

	condSlot := "./_for_cond_" + s.Var
	condBody := []ast.Stmt{
		infixInst("./"+s.Var, "<", s.End, condSlot),
		brInst(condSlot, bodyLabel, exitLabel),
	}

	stepBody := []ast.Stmt{
		infixInst("./"+s.Var, "+", "1", "./"+s.Var),
		jumpTo(condLabel),
	}

	return []ast.Stmt{
		&ast.BlockStmt{Label: initLabel, Body: initBody},
		&ast.BlockStmt{Label: condLabel, Body: condBody},
		&ast.BlockStmt{Label: bodyLabel, Body: append(lowerBody(s.Body, lg), jumpTo(stepLabel))},
		&ast.BlockStmt{Label: stepLabel, Body: stepBody},
		&ast.BlockStmt{Label: exitLabel, Body: nil},
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
	bodyStmts = append(bodyStmts, jumpTo(condLabel))

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

func jumpTo(label string) *ast.Instruction {
	return &ast.Instruction{Opcode: "jump", Reads: []string{label}}
}

func infixInst(left, op, right, write string) *ast.Instruction {
	return &ast.Instruction{Opcode: op, Reads: []string{left, right}, Writes: []string{write}}
}

func strSetInst(val, write string) *ast.Instruction {
	return &ast.Instruction{Opcode: "str.set", Reads: []string{val}, Writes: []string{write}}
}

func wrapBlock(label string, stmts []ast.Stmt, lg *labelGen) *ast.BlockStmt {
	if label == "" {
		label = lg.next("block")
	}
	return &ast.BlockStmt{Label: label, Body: stmts}
}
