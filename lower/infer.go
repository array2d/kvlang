// infer.go: 类型推断——为函数体中所有中间变量推导类型。
//
// 推断规则（按优先级）：
//   1. FuncSig 中声明的参数/返回值类型 → 直接使用
//   2. 算术算子（+ - * / % << >> & | ^）→ int/float，依赖操作数类型
//   3. 比较/逻辑算子（== != < > <= >= && || !）→ bool
//   4. 类型转换（int float float32 int8 …）→ 目标类型名即 opcode
//   5. 拷贝（=）→ 继承源操作数类型
//   6. 字面量读参 → 从字面量格式推断
//   7. 数组/字典 → "array"/"dict"
//   8. 内建函数（kvhas→bool, kvlen→int）
//   9. 其余 → ""（未知，留空）
//
// 调用方（layoutrwir）对空类型回退为 "rwir"。
package lower

import (
	"strings"

	"kvlang/ast"
	"kvlang/symbol"
	"kvlang/rwir"
)

// InferTypes 为单个函数体中的所有中间变量推断类型。
// 返回 typeMap：变量名 → 类型字符串。
func InferTypes(fn *ast.Func) map[string]string {
	tm := make(map[string]string)
	for _, p := range fn.Sig.Params {
		if p.Type != "" {
			tm[p.Name] = p.Type
		}
	}
	for _, p := range fn.Sig.Returns {
		if p.Type != "" {
			tm[p.Name] = p.Type
		}
	}
	inferBody(fn.Body, tm)
	return tm
}

func inferBody(body []ast.Stmt, tm map[string]string) {
	for _, st := range body {
		switch s := st.(type) {
		case *ast.Instruction:
			inferInst(s, tm)
		case *ast.ScopeStmt:
			inferBody(s.Body, tm)
		case *ast.IfStmt:
			if s.Cond != nil {
				inferInst(s.Cond, tm)
			}
			inferBody(s.Then, tm)
			inferBody(s.Else, tm)
		case *ast.WhileStmt:
			if s.Cond != nil {
				inferInst(s.Cond, tm)
			}
			inferBody(s.Body, tm)
		case *ast.ForStmt:
			inferBody(s.Body, tm)
		}
	}
}

// arithmeticOps 委托给 ops 包，禁止 hardcode。

// comparisonOps 委托给 ops 包，禁止 hardcode。

// castOps 是类型转换算子，opcode 即目标类型。
var castOps = map[string]bool{
	"int8": true, "int16": true, "int32": true, "int64": true,
	"uint8": true, "uint16": true, "uint32": true, "uint64": true,
	"float32": true, "float64": true,
}

func inferInst(inst *ast.Instruction, tm map[string]string) {
	if inst.Expr == nil || len(inst.Writes) == 0 {
		return
	}
	// 快速路径：叶节点字面量直接从 Lit 字段获取类型，无需 Flat() → slotType() 字符串重解析。
	if inst.Expr.IsLeaf() && inst.Expr.Lit != ast.LitNone {
		if inferred := litToType(inst.Expr.Lit); inferred != "" {
			for _, w := range inst.Writes {
				if w == "" || w[0] == '.' { continue }
				if _, exists := tm[w]; !exists { tm[w] = inferred }
			}
		}
		return
	}

	opcode, reads := inst.Flat()
	if opcode == "" {
		return
	}

	inferred := inferOpType(opcode, reads, tm)
	if inferred == "" {
		return
	}

	for _, w := range inst.Writes {
		if w == "" || w[0] == '.' {
			continue
		}
		if _, exists := tm[w]; !exists {
			tm[w] = inferred
		}
	}
}

// inferOpType 根据 opcode 和读参类型推断写参类型。
func inferOpType(opcode string, reads []string, tm map[string]string) string {
	// 控制流原语无写参类型
	if opcode == rwir.OpReturn || opcode == rwir.OpGoto || opcode == rwir.OpBr ||
		opcode == rwir.OpCall {
		return ""
	}

	// 拷贝：继承源操作数类型
	if symbol.Lookup(opcode).Word == "assign" {
		if len(reads) > 0 {
			return slotType(reads[0], tm)
		}
		return ""
	}

	// 算术算子：从操作数类型推断 int/float
	if symbol.Lookup(opcode).Arith {
		for _, r := range reads {
			if slotType(r, tm) == "float64" {
				return "float64"
			}
		}
		return "int64"
	}

	// 比较/逻辑算子 → bool
	if symbol.Lookup(opcode).Cmp {
		return "bool"
	}

	// 类型转换：opcode 即目标类型
	if castOps[opcode] {
		return opcode
	}

	// 字典字面量
	if opcode == "dict" {
		return "dict"
	}

	// 内建函数
	switch opcode {
	case "kvhas":
		return "bool"
	case "kvlen":
		return "int64"
	case "kvat":
		// at 的返回类型取决于存储的值，无法静态推断
		return ""
	case "set":
		// set 回写原值，类型同 base
		if len(reads) > 0 {
			return slotType(reads[0], tm)
		}
	}

	return ""
}

// slotType 返回槽的类型：优先从 typeMap 查，其次从字面量格式推断。
func slotType(name string, tm map[string]string) string {
	if name == "" {
		return ""
	}
	if t, ok := tm[name]; ok {
		return t
	}
	// 字面量推断
	if len(name) > 0 && name[0] == '"' {
		return "stringbyte"
	}
	if name == "true" || name == "false" {
		return "bool"
	}
	if len(name) > 0 && (name[0] >= '0' && name[0] <= '9' ||
		(name[0] == '-' && len(name) > 1 && name[1] >= '0' && name[1] <= '9')) {
		if strings.Contains(name, ".") || strings.ContainsAny(name, "eE") {
			return "float64"
		}
		return "int64"
	}
	return ""
}

// litToType 将 LitKind 映射为类型字符串，跳过 Flat() → slotType() 字符串重解析。
func litToType(lit ast.LitKind) string {
	switch lit {
	case ast.LitInt:    return "int64"
	case ast.LitFloat:  return "float64"
	case ast.LitString, ast.LitRawString: return "stringbyte"
	case ast.LitBool:   return "bool"
	case ast.LitNil:    return ""
	}
	return ""
}
