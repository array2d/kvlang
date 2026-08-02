// Package ast 定义 kvlang 抽象语法树节点类型。
// 纯数据结构包，不依赖存储层。
package ast

import (
	"strings"

	"kvlang/keytree"
	"kvlang/symbol"
)

// Stmt 表示函数体中的一条语句。
type Stmt interface {
	stmt()
	String() string
	FirstLine() string
}

// ── 签名 ──────────────────────────────────────────────────────

// Param 表示有名称和可选类型标注的形参。
type Param struct {
	Name string
	Type string // "" = 动态类型
}

// FuncSig 是函数签名的结构化形式。
type FuncSig struct {
	Name    string
	Params  []Param
	Returns []Param
}

// String 重建规范签名字符串：rwfunc name(A:int64, B:int64) -> (C:int64)
func (s FuncSig) String() string { return s.sigString("rwfunc") }

func (s FuncSig) sigString(prefix string) string {
	var sb strings.Builder
	sb.WriteString(prefix)
	sb.WriteByte(' ')
	sb.WriteString(s.Name)
	sb.WriteByte('(')
	for i, p := range s.Params {
		if i > 0 {
			sb.WriteString(", ")
		}
		sb.WriteString(p.Name)
		if p.Type != "" {
			sb.WriteByte(':')
			sb.WriteString(p.Type)
		}
	}
	sb.WriteString(") -> (")
	for i, p := range s.Returns {
		if i > 0 {
			sb.WriteString(", ")
		}
		sb.WriteString(p.Name)
		if p.Type != "" {
			sb.WriteByte(':')
			sb.WriteString(p.Type)
		}
	}
	sb.WriteByte(')')
	return sb.String()
}

// ParamNames 返回输入参数名列表。
func (s FuncSig) ParamNames() []string {
	names := make([]string, len(s.Params))
	for i, p := range s.Params {
		names[i] = p.Name
	}
	return names
}

// NumReads 返回读参（输入参数）数量。
func (s FuncSig) NumReads() int32 { return int32(len(s.Params)) }

// NumWrites 返回写参（输出参数）数量。
func (s FuncSig) NumWrites() int32 { return int32(len(s.Returns)) }

// ReturnNames 返回输出参数名列表。
func (s FuncSig) ReturnNames() []string {
	names := make([]string, len(s.Returns))
	for i, p := range s.Returns {
		names[i] = p.Name
	}
	return names
}

// ── 函数 ──────────────────────────────────────────────────────

// Func 表示一个函数定义。
type Func struct {
	Comments []string // 函数前的行注释
	Sig      FuncSig
	Body     []Stmt
	Pkg      string   // 所属 lib 包名（嵌套 lib 时 rwfunc 带上所在 lib 的 pkg；fix-039）
}

// RwirDecl 表示一个 rwir 声明（签名，无体，解释器直执行）。
type RwirDecl struct {
	Comments []string // 声明前的行注释
	Sig      FuncSig
	Pkg      string   // 所属 lib 包名
}

// SigString 返回 "rwir name(params) -> (returns)" 格式的签名字符串。
func (d RwirDecl) SigString() string { return d.Sig.sigString("rwir") }
func (fn *Func) FullText() string {
	var sb strings.Builder
	for _, c := range fn.Comments {
		sb.WriteString(c)
		sb.WriteString("\n")
	}
	sb.WriteString(fn.Sig.String())
	sb.WriteString(" {\n")
	for _, st := range fn.Body {
		for _, c := range StmtComments(st) {
			sb.WriteString("    ")
			sb.WriteString(c)
			sb.WriteString("\n")
		}
		sb.WriteString("    ")
		sb.WriteString(st.String())
		sb.WriteString("\n")
	}
	sb.WriteString("}")
	return sb.String()
}

// ── Expr ──────────────────────────────────────────────────────

// LitKind 标记叶节点的字面量类型。
type LitKind uint8

const (
	LitNone      LitKind = iota // 非字面量（变量引用、路径）
	LitInt                      // 整数字面量
	LitFloat                    // 浮点字面量
	LitString                   // 双引号/单引号字符串字面量
	LitRawString                // 反引号原始字符串字面量
	LitBool                     // 布尔字面量（true, false）
	LitNil                      // None 字面量
)

func (k LitKind) String() string {
	switch k {
	case LitNone:
		return "none"
	case LitInt:
		return "int"
	case LitFloat:
		return "float"
	case LitString:
		return "string"
	case LitRawString:
		return "rawstring"
	case LitBool:
		return "bool"
	case LitNil:
		return "nil"
	}
	return "unknown"
}

// Expr 是 kvlang 表达式树节点（由 Pratt 解析器生成）。
//
// 叶节点（IsLeaf()==true）：Op == ""，Val 为操作数字符串。
// Lit 携带字面量类型分类（LitNone=非字面量如变量/路径）。
//
// 内节点（IsLeaf()==false）：Op 为算子/函数名，Args 为操作数列表。
type Expr struct {
	Op    string  // 算子（"+"）/ 函数名（"f"）/ ""（叶节点）
	Args  []*Expr // 操作数（叶节点时为 nil）
	Val   string  // 叶节点值
	Quote byte    // 0=非字符串, '"'="..." 双引号, '`'=`...` 反引号
	Lit   LitKind // 字面量类型（仅叶节点有意义；LitNone=变量/路径）
}

// IsLeaf 判断是否为叶节点。
func (e *Expr) IsLeaf() bool { return e != nil && e.Op == "" }

// Leaf 构造变量/路径叶节点（非字面量）。
func Leaf(v string) *Expr { return &Expr{Val: v} }

// StrLit 构造双引号字符串字面量叶节点。
func StrLit(v string) *Expr { return &Expr{Val: v, Quote: '"', Lit: LitString} }

// RawStr 构造反引号原始字符串字面量叶节点（跨行，零转义）。
func RawStr(v string) *Expr { return &Expr{Val: v, Quote: '`', Lit: LitRawString} }

// IntLit 构造整数字面量叶节点。
func IntLit(v string) *Expr { return &Expr{Val: v, Lit: LitInt} }

// FloatLit 构造浮点字面量叶节点。
func FloatLit(v string) *Expr { return &Expr{Val: v, Lit: LitFloat} }

// BoolLit 构造布尔字面量叶节点。
func BoolLit(v string) *Expr { return &Expr{Val: v, Lit: LitBool} }

// NoneLit 构造 None 字面量叶节点。
func NoneLit() *Expr { return &Expr{Val: "None", Lit: LitNil} }

// Call 构造调用/操作节点（算子或函数名 + 操作数列表）。
func Call(op string, args ...*Expr) *Expr { return &Expr{Op: op, Args: args} }

// InfixPrec 返回算子的中缀优先级（0 = 非中缀/不支持）。
// 委托给 ops 包，禁止在此 hardcode 优先级。
func InfixPrec(op string) int { return symbol.Lookup(op).Precedence }

// String 返回表达式的规范文本表示（保留优先级，必要时添加括号）。
func (e *Expr) String() string {
	if e == nil {
		return ""
	}
	return e.stringPrec(0)
}

func (e *Expr) stringPrec(outerPrec int) string {
	if e == nil {
		return "<?>"
	}
	if e.IsLeaf() {
		if e.Quote != 0 {
			if e.Quote == '"' {
			return "\"" + escapeString(e.Val) + "\""
		}
		return "`" + e.Val + "`"
		}
		return e.Val
	}
	// 二元中缀
	if p := symbol.Lookup(e.Op).Precedence; p > 0 && len(e.Args) == 2 {
		left := e.Args[0].stringPrec(p)
		right := e.Args[1].stringPrec(p + 1)
		s := left + " " + e.Op + " " + right
		if outerPrec > p {
			return "(" + s + ")"
		}
		return s
	}
	// 一元前缀运算符: -x, !x（仅符号算子，不含函数名）
	if len(e.Args) == 1 && isOperatorChar(e.Op[0]) {
		return e.Op + e.Args[0].stringPrec(200)
	}
	// 函数调用（0/1/多参统一用括号）: foo(), print("x"), add(a, b)
	args := make([]string, len(e.Args))
	for i, a := range e.Args {
		args[i] = a.String()
	}
	return e.Op + "(" + strings.Join(args, ", ") + ")"
}

// ── Instruction ───────────────────────────────────────────────

// Instruction 表示一条 kvlang 指令。
// Expr 是 Pratt 解析的表达式树；Writes 是写目标槽列表。
type Instruction struct {
	Comments   []string // 该指令前的行注释
	Expr       *Expr    // 表达式（nil 表示空指令）
	Writes     []string // 写目标（位置：裸名、/abs、base.名）
	WriteTypes []string // 写槽类型标注（与 Writes 平行；"" = 无标注）
	ArrowLeft  bool     // true = 写槽在左（<- 或 =），false = ->
	Eq         bool     // true = 源码用 = 书写（≡ <-，仅影响还原渲染）
}

// leftArrow 返回写槽在左时的算子渲染形态。
func (i *Instruction) leftArrow() string {
	if i.Eq { return symbol.ArrowEq }
	return symbol.ArrowLeft
}

// Flat 返回用于 KV 布局的扁平 (opcode, reads) 表示。
// 前提：lower 已将复合子表达式展开，所有 Args 均为叶节点。
//
// 归一化规则（仅对 Leaf opcode 生效）：
//   叶节点（a、42、"s"、/abs）→ 显式拷贝操作码 "="，值引用放读槽
//   以此区分 "a -> b"（copy, opcode="=", reads=["a"]）
//   与 "greet() -> x"（zero-arg call, opcode="greet"，来自 Call 节点）
func (i *Instruction) Flat() (opcode string, reads []string) {
	if i.Expr == nil {
		return "", nil
	}
	if i.Expr.IsLeaf() {
		v := i.Expr.Val
		if v == "return" {
			return "return", nil
		}
		// 字符串字面量 → " 前缀（KV 传输标记）
		if i.Expr.Quote != 0 {
			return "=", []string{"\"" + v}
		}
		return "=", []string{v}
	}
	opcode = i.Expr.Op
	for _, arg := range i.Expr.Args {
		r := arg.Val
		if arg.Quote != 0 {
			r = "\"" + r
		}
		reads = append(reads, r)
	}
	return
}

func (*Instruction) stmt() {}
func (i *Instruction) FirstLine() string { return i.String() }
func (i *Instruction) String() string {
	e := i.Expr
	if e == nil {
		return ""
	}
	writes := i.joinTypedWrites()
	// array(...) → [...]
	if e.Op == "array" {
		args := make([]string, len(e.Args))
		for j, a := range e.Args {
			args[j] = a.String()
		}
		s := "[" + strings.Join(args, ", ") + "]"
		if len(i.Writes) > 0 {
			if i.ArrowLeft {
				return writes + i.leftArrow() + s
			}
			return s + symbol.ArrowRight + writes
		}
		return s
	}
	// dict("k1", v1, "k2", v2, ...) → { k1=v1; k2=v2 }
	if e.Op == "dict" {
		var pairs []string
		for j := 0; j+1 < len(e.Args); j += 2 {
			pairs = append(pairs, e.Args[j].Val+"="+e.Args[j+1].String())
		}
		s := "{ " + strings.Join(pairs, "; ") + " }"
		if len(i.Writes) > 0 {
			if i.ArrowLeft {
				return writes + i.leftArrow() + s
			}
			return s + symbol.ArrowRight + writes
		}
		return s
	}
	// set(base, idx, val) → a[idx] <- val ( <- form only;
	// -> form stays as set() call since parser can't parse val -> a[idx])
	if e.Op == "set" && len(e.Args) >= 3 && i.ArrowLeft {
		base := e.Args[0].String()
		idx := idxString(e.Args[1])
		val := e.Args[2].String()
		return base + "[" + idx + "]" + i.leftArrow() + val
	}
	s := e.String()
	// at(base, idx) → base[idx] or base.field
	if e.Op == "at" && len(e.Args) >= 2 {
		if e.Args[1].Quote == '"' {
			s = e.Args[0].String() + keytree.MemberSep + e.Args[1].Val
		} else {
			s = e.Args[0].String() + "[" + e.Args[1].String() + "]"
		}
	}
	if len(i.Writes) > 0 {
		if i.ArrowLeft {
			s = writes + i.leftArrow() + s
		} else {
			s += symbol.ArrowRight + writes
		}
	}
	return s
}

// joinTypedWrites 将写槽列表格式化：name:type 或裸名。单槽直接，多槽加括号。
func (i *Instruction) joinTypedWrites() string {
	if len(i.Writes) == 0 {
		return ""
	}
	parts := make([]string, len(i.Writes))
	for j, w := range i.Writes {
		if j < len(i.WriteTypes) && i.WriteTypes[j] != "" {
			parts[j] = w + ":" + i.WriteTypes[j]
		} else {
			parts[j] = w
		}
	}
	if len(parts) == 1 {
		return parts[0]
	}
	return "(" + strings.Join(parts, ", ") + ")"
}

// idxString formats an index expression for [] syntax.
func idxString(e *Expr) string {
	if e.Quote == '"' {
		return "\"" + e.Val + "\""
	}
	return e.String()
}

// ── 控制流节点 ────────────────────────────────────────────────

// IfStmt 表示 if/else 控制流。
type IfStmt struct {
	Comments []string
	Cond     *Instruction // 条件表达式，lower 前 Writes 为 nil
	Then     []Stmt
	Else     []Stmt
}

func (*IfStmt) stmt() {}
func (*IfStmt) FirstLine() string { return "if" }
func (s *IfStmt) String() string {
	cond := ""
	if s.Cond != nil {
		cond = s.Cond.String()
	}
	r := "if (" + cond + ") {\n"
	for _, st := range s.Then {
		r += "\t" + st.String() + "\n"
	}
	r += "}"
	if len(s.Else) > 0 {
		r += " else {\n"
		for _, st := range s.Else {
			r += "\t" + st.String() + "\n"
		}
		r += "}"
	}
	return r
}

// ForStmt 表示 for 循环：迭代 kvspace 路径上的数据。
type ForStmt struct {
	Comments []string
	Var      string
	Iter     string
	Body     []Stmt
}

func (*ForStmt) stmt() {}
func (*ForStmt) FirstLine() string { return "for" }
func (s *ForStmt) String() string {
	r := "for (" + s.Var + " in " + s.Iter + ") {\n"
	for _, st := range s.Body {
		r += "\t" + st.String() + "\n"
	}
	return r + "}"
}

// WhileStmt 表示 while 循环。
type WhileStmt struct {
	Comments []string
	Cond     *Instruction
	Body     []Stmt
}

func (*WhileStmt) stmt() {}
func (*WhileStmt) FirstLine() string { return "while" }
func (s *WhileStmt) String() string {
	cond := ""
	if s.Cond != nil {
		cond = s.Cond.String()
	}
	r := "while (" + cond + ") {\n"
	for _, st := range s.Body {
		r += "\t" + st.String() + "\n"
	}
	return r + "}"
}

// BreakStmt 表示跳出当前循环。
type BreakStmt struct {
	Comments []string
}

func (*BreakStmt) stmt()             {}
func (*BreakStmt) FirstLine() string { return "break" }
func (*BreakStmt) String() string    { return "break" }

// ContinueStmt 表示跳过本次迭代。
type ContinueStmt struct {
	Comments []string
}

func (*ContinueStmt) stmt()             {}
func (*ContinueStmt) FirstLine() string { return "continue" }
func (*ContinueStmt) String() string    { return "continue" }

// ScopeStmt 表示一个带标签的基本块。
type ScopeStmt struct {
	Comments []string
	Label    string
	Body     []Stmt
}

func (*ScopeStmt) stmt() {}
func (s *ScopeStmt) FirstLine() string { return s.Label }
func (s *ScopeStmt) String() string {
	r := s.Label + ": {\n"
	for _, st := range s.Body {
		r += "\t" + st.String() + "\n"
	}
	return r + "}"
}

// StmtComments 返回语句的前置行注释（供 Format/FullText 使用）。
func StmtComments(st Stmt) []string {
	switch s := st.(type) {
	case *Instruction:
		return s.Comments
	case *IfStmt:
		return s.Comments
	case *ForStmt:
		return s.Comments
	case *WhileStmt:
		return s.Comments
	case *BreakStmt:
		return s.Comments
	case *ContinueStmt:
		return s.Comments
	case *ScopeStmt:
		return s.Comments
	}
	return nil
}

// ── 工具函数 ──────────────────────────────────────────────────

// isOperatorChar reports whether c is a punctuation operator (not a function name).
func isOperatorChar(c byte) bool {
	for _, b := range symbol.ScannerOneCharOps() {
		if c == b { return true }
	}
	return false
}
