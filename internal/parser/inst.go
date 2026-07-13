package parser

import (
	"strings"

	"kvlang/internal/ast"
)

// ParseInst 从 Token 流（不含 Newline/EOF）构建 *ast.Instruction。
//
// 支持三种赋值风格:
//
//	前缀（命名函数）: add(A, B) -> ./C
//	中缀（符号算子）: A + B -> ./C, !A -> ./C
//	C 风格（左箭头）: ./C <- A + B, ./C <- add(A, B)
func ParseInst(toks []Token) (*ast.Instruction, error) {
	inst := &ast.Instruction{}

	// 1. 找第一个 Arrow，分割 expr / writes
	arrowIdx := -1
	for i, t := range toks {
		if t.Kind == Arrow {
			arrowIdx = i
			break
		}
	}

	var exprToks, writeToks []Token
	if arrowIdx >= 0 {
		if toks[arrowIdx].Value == "<-" {
			writeToks = toks[:arrowIdx]
			exprToks = toks[arrowIdx+1:]
		} else { // "->"
			exprToks = toks[:arrowIdx]
			writeToks = toks[arrowIdx+1:]
		}
	} else {
		exprToks = toks
	}

	// 2. 解析写槽（去除外层括号后按逗号收集）
	inst.Writes = collectParams(stripOuterParens(writeToks))

	// 3. 解析表达式
	if len(exprToks) == 0 {
		return inst, nil
	}
	first := exprToks[0]

	// 3a. 前缀调用：opcode(args...)  — 含 return(./Z), br(...) 等
	if len(exprToks) > 1 && exprToks[1].Kind == LParen {
		inst.Opcode = first.Value
		inst.Reads = collectParams(innerArgs(exprToks[2:]))
		return inst, nil
	}

	// 3b. 中缀算子：A op B
	if len(exprToks) >= 2 && isInfixOp(exprToks[1].Value) {
		inst.Opcode = exprToks[1].Value
		inst.Reads = append(inst.Reads, first.Value)
		if len(exprToks) > 2 {
			// 右操作数：将剩余 Token 值拼接（处理 -B 等情况）
			var sb strings.Builder
			for _, t := range exprToks[2:] {
				sb.WriteString(t.Value)
			}
			inst.Reads = append(inst.Reads, sb.String())
		}
		return inst, nil
	}

	// 3c. 一元前缀算子：!A 或 -A
	if isUnaryPrefixOp(first.Value) && len(exprToks) >= 2 {
		inst.Opcode = first.Value
		inst.Reads = append(inst.Reads, exprToks[1].Value)
		return inst, nil
	}

	// 3d. 裸操作码（zeros, break, continue, return 等）
	inst.Opcode = first.Value
	return inst, nil
}

// ── Token helpers ──

// stripOuterParens 去除首尾的 LParen / RParen（用于多写槽 (a, b)）。
func stripOuterParens(toks []Token) []Token {
	if len(toks) >= 2 && toks[0].Kind == LParen && toks[len(toks)-1].Kind == RParen {
		return toks[1 : len(toks)-1]
	}
	return toks
}

// innerArgs 去除函数调用尾部的 RParen（exprToks[2:] 传入时用）。
func innerArgs(toks []Token) []Token {
	if len(toks) > 0 && toks[len(toks)-1].Kind == RParen {
		return toks[:len(toks)-1]
	}
	return toks
}

// collectParams 从已分隔的 Token 列表中收集参数值（跳过 Comma）。
func collectParams(toks []Token) []string {
	var result []string
	for _, t := range toks {
		if t.Kind == Comma {
			continue
		}
		result = append(result, t.Value)
	}
	return result
}

// infixOpSet 包含全部二元中缀算子。
var infixOpSet = map[string]bool{
	"+": true, "-": true, "*": true, "/": true, "%": true,
	"==": true, "!=": true, "<": true, ">": true, "<=": true, ">=": true,
	"&&": true, "||": true, "&": true, "|": true, "^": true, "<<": true, ">>": true,
}

// isInfixOp 判断操作符字符串是否为二元中缀算子。
func isInfixOp(s string) bool { return infixOpSet[s] }

// isUnaryPrefixOp 判断操作符字符串是否为一元前缀算子。
func isUnaryPrefixOp(s string) bool {
	return s == "!" || s == "-"
}
