// Package ast 定义 kvlang 抽象语法树节点类型。
// 纯数据结构包，不依赖存储层。
package ast

import (
	"strings"
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

// String 重建规范签名字符串：def name(A:int, B:int) -> (C:int)
func (s FuncSig) String() string {
	var sb strings.Builder
	sb.WriteString("def ")
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
}

// FullText 返回函数的完整源码文本，用于存入 /src/<pkg>/<name>。
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

// Expr 是 kvlang 表达式树节点（由 Pratt 解析器生成）。
//
// 叶节点（IsLeaf()==true）：Op == ""，Val 为操作数字符串。
// Quote 区分字符串字面量（true）和变量/数字/路径（false）。
//
// 内节点（IsLeaf()==false）：Op 为算子/函数名，Args 为操作数列表。
type Expr struct {
	Op    string  // 算子（"+"）/ 函数名（"f"）/ ""（叶节点）
	Args  []*Expr // 操作数（叶节点时为 nil）
	Val   string  // 叶节点值
	Quote byte    // 0=非字符串, '"'="..." 双引号, '`'=`...` 反引号
}

// IsLeaf 判断是否为叶节点。
func (e *Expr) IsLeaf() bool { return e != nil && e.Op == "" }

// Leaf 构造变量/数字/路径叶节点。
func Leaf(v string) *Expr { return &Expr{Val: v} }

// StrLit 构造双引号字符串字面量叶节点。
func StrLit(v string) *Expr { return &Expr{Val: v, Quote: '"'} }

// RawStr 构造反引号原始字符串字面量叶节点（跨行，零转义）。
func RawStr(v string) *Expr { return &Expr{Val: v, Quote: '`'} }

// Call 构造调用/操作节点（算子或函数名 + 操作数列表）。
func Call(op string, args ...*Expr) *Expr { return &Expr{Op: op, Args: args} }

// infixPrecTable 中缀算子优先级表（值越大优先级越高）。
var infixPrecTable = map[string]int{
	"||": 10, "&&": 20,
	"==": 30, "!=": 30,
	"<": 40, ">": 40, "<=": 40, ">=": 40,
	"+": 50, "-": 50,
	"*": 60, "/": 60, "%": 60,
	"<<": 70, ">>": 70,
	"&": 80, "^": 90, "|": 100,
}

// InfixPrec 返回算子的中缀优先级（0 = 非中缀/不支持）。
func InfixPrec(op string) int { return infixPrecTable[op] }

// String 返回表达式的规范文本表示（保留优先级，必要时添加括号）。
func (e *Expr) String() string {
	if e == nil {
		return ""
	}
	return e.stringPrec(0)
}

func (e *Expr) stringPrec(outerPrec int) string {
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
	if p, ok := infixPrecTable[e.Op]; ok && len(e.Args) == 2 {
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
	Comments  []string // 该指令前的行注释
	Expr      *Expr    // 表达式（nil 表示空指令）
	Writes    []string // 写目标（槽路径，如 ./x、/abs）
	ArrowLeft bool     // true = <-, false = ->
}

// Flat 返回用于 KV 布局的扁平 (opcode, reads) 表示。
// 前提：lower 已将复合子表达式展开，所有 Args 均为叶节点。
//
// 归一化规则（仅对 Leaf opcode 生效）：
//   裸标识符（如 a、result）→ "./ident"（本帧相对路径）
//   以此区分 "a -> b"（copy, opcode="./a"）
//   与 "greet() -> ./x"（zero-arg call, opcode="greet"，来自 Call 节点）
func (i *Instruction) Flat() (opcode string, reads []string) {
	if i.Expr == nil {
		return "", nil
	}
	if i.Expr.IsLeaf() {
		v := i.Expr.Val
		// 字符串字面量 → " 前缀（KV 传输标记）
		if i.Expr.Quote != 0 {
			return "\"" + v, nil
		}
		// 裸标识符 → ./ident
		if isBareIdentVal(v) {
			return "./" + v, nil
		}
		return v, nil
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

// isBareIdentVal 判断字符串是否为裸标识符（首字母/下划线，其余字母数字下划线）。
// 用于 Flat() 中将 Leaf("a") 归一化为 "./a"。
func isBareIdentVal(v string) bool {
	if len(v) == 0 {
		return false
	}
	// 排除特殊前缀
	if v[0] == '"' || v[0] == '/' {
		return false
	}
	if len(v) >= 2 && v[:2] == "./" {
		return false
	}
	// 排除布尔字面量
	if v == "true" || v == "false" {
		return false
	}
	// 排除数字字面量（首字符为数字）
	if v[0] >= '0' && v[0] <= '9' {
		return false
	}
	// 排除操作符（含 +、-、*、/ 等）
	c0 := v[0]
	if !((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z') || c0 == '_') {
		return false
	}
	for i := 1; i < len(v); i++ {
		c := v[i]
		if !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			return false
		}
	}
	return true
}

func (*Instruction) stmt() {}
func (i *Instruction) FirstLine() string { return i.String() }
func (i *Instruction) String() string {
	e := i.Expr
	if e == nil {
		return ""
	}
	// array(...) → [...]
	if e.Op == "array" {
		args := make([]string, len(e.Args))
		for j, a := range e.Args {
			args[j] = a.String()
		}
		s := "[" + strings.Join(args, ", ") + "]"
		if len(i.Writes) > 0 {
			if i.ArrowLeft {
				return joinWrites(i.Writes) + " <- " + s
			}
			return s + " -> " + joinWrites(i.Writes)
		}
		return s
	}
	// set(base, idx, val) → a[idx] <- val ( <- form only;
	// -> form stays as set() call since parser can't parse val -> a[idx])
	if e.Op == "set" && len(e.Args) >= 3 && i.ArrowLeft {
		base := e.Args[0].String()
		idx := idxString(e.Args[1])
		val := e.Args[2].String()
		return base + "[" + idx + "] <- " + val
	}
	s := e.String()
	// at(base, idx) → base[idx] or base.field
	if e.Op == "at" && len(e.Args) >= 2 {
		if e.Args[1].Quote == '"' {
			s = e.Args[0].String() + "." + e.Args[1].Val
		} else {
			s = e.Args[0].String() + "[" + e.Args[1].String() + "]"
		}
	}
	if len(i.Writes) > 0 {
		if i.ArrowLeft {
			s = joinWrites(i.Writes) + " <- " + s
		} else {
			s += " -> " + joinWrites(i.Writes)
		}
	}
	return s
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

// BlockStmt 表示一个带标签的基本块。
type BlockStmt struct {
	Comments []string
	Label    string
	Body     []Stmt
}

func (*BlockStmt) stmt() {}
func (s *BlockStmt) FirstLine() string { return s.Label }
func (s *BlockStmt) String() string {
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
	case *BlockStmt:
		return s.Comments
	}
	return nil
}

// ── 工具函数 ──────────────────────────────────────────────────

// joinWrites 将写槽列表格式化：单槽直接输出，多槽加括号。
func joinWrites(ss []string) string {
	if len(ss) == 0 {
		return ""
	}
	if len(ss) == 1 {
		return ss[0]
	}
	return "(" + strings.Join(ss, ", ") + ")"
}

// needsQuote 判断字符串在输出时是否需要引号包裹。
func needsQuote(s string) bool {
	if len(s) == 0 {
		return false
	}
	// 路径不需要引号
	if s[0] == '/' || (len(s) >= 2 && s[:2] == "./") {
		return false
	}
	for i := 0; i < len(s); i++ {
		switch s[i] {
		case ' ', '\t', ',', '(', ')', '=', '+', '-', '*', '%', '!', '<', '>', '&', '|', '^', '{', '}', '"', '\'':
			return true
		}
	}
	return false
}

// isOperatorChar reports whether c is a punctuation operator (not a function name).
func isOperatorChar(c byte) bool {
	switch c {
	case '+', '-', '*', '/', '%', '!', '=', '<', '>', '&', '|', '^':
		return true
	}
	return false
}
