//! AST 节点类型（对齐 ast/ast.go、astfile.go、escape.go）。纯数据结构，不依赖存储层。

use std::fmt;

use super::symbol;

// ── 语句 ─────────────────────────────────────────────────────────────

#[derive(Clone)]
pub enum Stmt {
    Instruction(Instruction),
    If(IfStmt),
    For(ForStmt),
    While(WhileStmt),
    Break(BreakStmt),
    Continue(ContinueStmt),
    Scope(ScopeStmt),
}

impl fmt::Display for Stmt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Stmt::Instruction(s) => write!(f, "{s}"),
            Stmt::If(s) => write!(f, "{s}"),
            Stmt::For(s) => write!(f, "{s}"),
            Stmt::While(s) => write!(f, "{s}"),
            Stmt::Break(s) => write!(f, "{s}"),
            Stmt::Continue(s) => write!(f, "{s}"),
            Stmt::Scope(s) => write!(f, "{s}"),
        }
    }
}

impl Stmt {
    pub fn first_line(&self) -> String {
        match self {
            Stmt::Instruction(s) => s.to_string(),
            Stmt::If(_) => "if".to_string(),
            Stmt::For(_) => "for".to_string(),
            Stmt::While(_) => "while".to_string(),
            Stmt::Break(_) => "break".to_string(),
            Stmt::Continue(_) => "continue".to_string(),
            Stmt::Scope(s) => s.label.clone(),
        }
    }
}

/// 返回语句的前置行注释（供 FullText 使用）。
pub fn stmt_comments(st: &Stmt) -> &[String] {
    match st {
        Stmt::Instruction(s) => &s.comments,
        Stmt::If(s) => &s.comments,
        Stmt::For(s) => &s.comments,
        Stmt::While(s) => &s.comments,
        Stmt::Break(s) => &s.comments,
        Stmt::Continue(s) => &s.comments,
        Stmt::Scope(s) => &s.comments,
    }
}

// ── 签名 ─────────────────────────────────────────────────────────────

#[derive(Clone)]
pub struct Param {
    pub name: String,
    pub ty: String, // "" = 动态类型
}

#[derive(Clone)]
pub struct FuncSig {
    pub name: String,
    pub params: Vec<Param>,
    pub returns: Vec<Param>,
}

impl FuncSig {
    fn sig_string(&self, prefix: &str) -> String {
        let mut sb = String::new();
        sb.push_str(prefix);
        sb.push(' ');
        sb.push_str(&self.name);
        sb.push('(');
        for (i, p) in self.params.iter().enumerate() {
            if i > 0 {
                sb.push_str(", ");
            }
            sb.push_str(&p.name);
            if !p.ty.is_empty() {
                sb.push(':');
                sb.push_str(&p.ty);
            }
        }
        sb.push_str(") -> (");
        for (i, p) in self.returns.iter().enumerate() {
            if i > 0 {
                sb.push_str(", ");
            }
            sb.push_str(&p.name);
            if !p.ty.is_empty() {
                sb.push(':');
                sb.push_str(&p.ty);
            }
        }
        sb.push(')');
        sb
    }

    pub fn param_names(&self) -> Vec<String> {
        self.params.iter().map(|p| p.name.clone()).collect()
    }

    /// 参数 kindexp 列表（读参在前、写参在后，源文法逐字节），落盘于 rwir/rwfunc body。
    pub fn kindexp_list(&self) -> Vec<String> {
        self.params
            .iter()
            .chain(self.returns.iter())
            .map(|p| p.ty.clone())
            .collect()
    }

    pub fn num_reads(&self) -> i32 {
        self.params.len() as i32
    }

    pub fn num_writes(&self) -> i32 {
        self.returns.len() as i32
    }
}

impl fmt::Display for FuncSig {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.sig_string("rwfunc"))
    }
}

// ── 函数 ─────────────────────────────────────────────────────────────

#[derive(Clone)]
pub struct Func {
    pub comments: Vec<String>,
    pub sig: FuncSig,
    pub body: Vec<Stmt>,
    pub pkg: String,
}

impl Func {
    pub fn full_text(&self) -> String {
        let mut sb = String::new();
        for c in &self.comments {
            sb.push_str(c);
            sb.push('\n');
        }
        sb.push_str(&self.sig.to_string());
        sb.push_str(" {\n");
        for st in &self.body {
            for c in stmt_comments(st) {
                sb.push_str("    ");
                sb.push_str(c);
                sb.push('\n');
            }
            sb.push_str("    ");
            sb.push_str(&st.to_string());
            sb.push('\n');
        }
        sb.push('}');
        sb
    }
}

#[derive(Clone)]
pub struct RwirDecl {
    pub comments: Vec<String>,
    pub sig: FuncSig,
    pub pkg: String,
}

impl RwirDecl {
    pub fn sig_string(&self) -> String {
        self.sig.sig_string("rwir")
    }
}

// ── Expr ─────────────────────────────────────────────────────────────

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum LitKind {
    LitNone,
    LitInt,
    LitFloat,
    LitString,
    LitRawString,
    LitBool,
    LitNil,
}

impl fmt::Display for LitKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            LitKind::LitNone => "none",
            LitKind::LitInt => "int",
            LitKind::LitFloat => "float",
            LitKind::LitString => "string",
            LitKind::LitRawString => "rawstring",
            LitKind::LitBool => "bool",
            LitKind::LitNil => "nil",
        };
        write!(f, "{s}")
    }
}

#[derive(Clone)]
pub struct Expr {
    pub op: String,       // 算子/函数名（"" = 叶节点）
    pub args: Vec<Expr>,  // 操作数（叶节点为空）
    pub val: String,      // 叶节点值
    pub quote: u8,        // 0=非字符串, '"'=双引号, '`'=反引号
    pub lit: LitKind,     // 字面量类型（仅叶节点有意义）
}

impl Expr {
    pub fn is_leaf(&self) -> bool {
        self.op.is_empty()
    }
}

pub fn leaf(v: &str) -> Expr {
    Expr { op: String::new(), args: Vec::new(), val: v.to_string(), quote: 0, lit: LitKind::LitNone }
}

pub fn str_lit(v: &str) -> Expr {
    Expr { op: String::new(), args: Vec::new(), val: v.to_string(), quote: b'"', lit: LitKind::LitString }
}

pub fn raw_str(v: &str) -> Expr {
    Expr { op: String::new(), args: Vec::new(), val: v.to_string(), quote: b'`', lit: LitKind::LitRawString }
}

pub fn int_lit(v: &str) -> Expr {
    Expr { op: String::new(), args: Vec::new(), val: v.to_string(), quote: 0, lit: LitKind::LitInt }
}

pub fn float_lit(v: &str) -> Expr {
    Expr { op: String::new(), args: Vec::new(), val: v.to_string(), quote: 0, lit: LitKind::LitFloat }
}

pub fn bool_lit(v: &str) -> Expr {
    Expr { op: String::new(), args: Vec::new(), val: v.to_string(), quote: 0, lit: LitKind::LitBool }
}

pub fn none_lit() -> Expr {
    Expr { op: String::new(), args: Vec::new(), val: "None".to_string(), quote: 0, lit: LitKind::LitNil }
}

pub fn call(op: &str, args: Vec<Expr>) -> Expr {
    Expr { op: op.to_string(), args, val: String::new(), quote: 0, lit: LitKind::LitNone }
}

impl Expr {
    /// 算子的中缀优先级（0 = 非中缀）。
    pub fn infix_prec(op: &str) -> i32 {
        symbol::lookup(op).precedence
    }

    fn string_prec(&self, outer_prec: i32) -> String {
        if self.is_leaf() {
            if self.quote != 0 {
                if self.quote == b'"' {
                    return format!("\"{}\"", escape_string(&self.val));
                }
                return format!("`{}`", self.val);
            }
            return self.val.clone();
        }
        let p = symbol::lookup(&self.op).precedence;
        if p > 0 && self.args.len() == 2 {
            let left = self.args[0].string_prec(p);
            let right = self.args[1].string_prec(p + 1);
            let s = format!("{left} {} {right}", self.op);
            if outer_prec > p {
                return format!("({s})");
            }
            return s;
        }
        if self.args.len() == 1 && is_operator_char(self.op.as_bytes()[0]) {
            return format!("{}{}", self.op, self.args[0].string_prec(200));
        }
        let args: Vec<String> = self.args.iter().map(|a| a.to_string()).collect();
        format!("{}({})", self.op, args.join(", "))
    }
}

impl fmt::Display for Expr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.string_prec(0))
    }
}

// ── Instruction ──────────────────────────────────────────────────────

#[derive(Clone, Default)]
pub struct Instruction {
    pub comments: Vec<String>,
    pub expr: Option<Expr>,  // None = 空指令
    pub writes: Vec<String>,
    pub write_types: Vec<String>,
    pub arrow_left: bool, // true = 写槽在左（<- 或 =）
    pub eq: bool,         // true = 源码用 = 书写
}

impl Instruction {
    fn left_arrow(&self) -> &'static str {
        if self.eq {
            symbol::ARROW_EQ
        } else {
            symbol::ARROW_LEFT
        }
    }

    /// 扁平化 (opcode, reads)。前提：lower 已把复合子表达式展开为叶节点。
    pub fn flat(&self) -> (String, Vec<String>) {
        let e = match &self.expr {
            Some(e) => e,
            None => return (String::new(), Vec::new()),
        };
        if e.is_leaf() {
            let v = &e.val;
            if v == "return" {
                return ("return".to_string(), Vec::new());
            }
            if e.quote != 0 {
                return ("=".to_string(), vec![format!("\"{v}")]);
            }
            return ("=".to_string(), vec![v.clone()]);
        }
        let opcode = e.op.clone();
        let reads: Vec<String> = e
            .args
            .iter()
            .map(|a| if a.quote != 0 { format!("\"{}", a.val) } else { a.val.clone() })
            .collect();
        (opcode, reads)
    }

    fn join_typed_writes(&self) -> String {
        if self.writes.is_empty() {
            return String::new();
        }
        let parts: Vec<String> = self
            .writes
            .iter()
            .enumerate()
            .map(|(j, w)| {
                if j < self.write_types.len() && !self.write_types[j].is_empty() {
                    format!("{w}:{}", self.write_types[j])
                } else {
                    w.clone()
                }
            })
            .collect();
        if parts.len() == 1 {
            parts[0].clone()
        } else {
            format!("({})", parts.join(", "))
        }
    }
}

fn idx_string(e: &Expr) -> String {
    if e.quote == b'"' {
        format!("\"{}\"", e.val)
    } else {
        e.to_string()
    }
}

impl fmt::Display for Instruction {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let e = match &self.expr {
            Some(e) => e,
            None => return write!(f, ""),
        };
        let writes = self.join_typed_writes();
        // array(...) → [...]
        if e.op == "array" {
            let args: Vec<String> = e.args.iter().map(|a| a.to_string()).collect();
            let s = format!("[{}]", args.join(", "));
            if !self.writes.is_empty() {
                if self.arrow_left {
                    return write!(f, "{writes}{}{s}", self.left_arrow());
                }
                return write!(f, "{s}{}{writes}", symbol::ARROW_RIGHT);
            }
            return write!(f, "{s}");
        }
        // dict("k1", v1, ...) → { k1=v1; k2=v2 }
        if e.op == "dict" {
            let mut pairs = Vec::new();
            let mut j = 0;
            while j + 1 < e.args.len() {
                pairs.push(format!("{}={}", e.args[j].val, e.args[j + 1]));
                j += 2;
            }
            let s = format!("{{ {} }}", pairs.join("; "));
            if !self.writes.is_empty() {
                if self.arrow_left {
                    return write!(f, "{writes}{}{s}", self.left_arrow());
                }
                return write!(f, "{s}{}{writes}", symbol::ARROW_RIGHT);
            }
            return write!(f, "{s}");
        }
        // set(base, idx, val) → a[idx] <- val（仅 <- 形式）
        if e.op == "set" && e.args.len() >= 3 && self.arrow_left {
            let base = e.args[0].to_string();
            let idx = idx_string(&e.args[1]);
            let val = e.args[2].to_string();
            return write!(f, "{base}[{idx}]{}{val}", self.left_arrow());
        }
        let mut s = e.to_string();
        // at(base, idx) → base[idx] 或 base.field
        if e.op == "at" && e.args.len() >= 2 {
            if e.args[1].quote == b'"' {
                s = format!("{}.{}", e.args[0], e.args[1].val);
            } else {
                s = format!("{}[{}]", e.args[0], e.args[1]);
            }
        }
        if !self.writes.is_empty() {
            if self.arrow_left {
                s = format!("{writes}{}{s}", self.left_arrow());
            } else {
                s = format!("{s}{}{writes}", symbol::ARROW_RIGHT);
            }
        }
        write!(f, "{s}")
    }
}

// ── 控制流节点 ──────────────────────────────────────────────────────

#[derive(Clone)]
pub struct IfStmt {
    pub comments: Vec<String>,
    pub cond: Option<Instruction>,
    pub then_: Vec<Stmt>,
    pub else_: Vec<Stmt>,
}

impl fmt::Display for IfStmt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let cond = self.cond.as_ref().map(|c| c.to_string()).unwrap_or_default();
        let mut r = format!("if ({cond}) {{\n");
        for st in &self.then_ {
            r.push_str(&format!("\t{st}\n"));
        }
        r.push('}');
        if !self.else_.is_empty() {
            r.push_str(" else {\n");
            for st in &self.else_ {
                r.push_str(&format!("\t{st}\n"));
            }
            r.push('}');
        }
        write!(f, "{r}")
    }
}

#[derive(Clone)]
pub struct ForStmt {
    pub comments: Vec<String>,
    pub var: String,
    pub iter: Expr,
    pub body: Vec<Stmt>,
}

impl fmt::Display for ForStmt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut r = format!("for ({} in {}) {{\n", self.var, self.iter);
        for st in &self.body {
            r.push_str(&format!("\t{st}\n"));
        }
        write!(f, "{r}}}")
    }
}

#[derive(Clone)]
pub struct WhileStmt {
    pub comments: Vec<String>,
    pub cond: Option<Instruction>,
    pub body: Vec<Stmt>,
}

impl fmt::Display for WhileStmt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let cond = self.cond.as_ref().map(|c| c.to_string()).unwrap_or_default();
        let mut r = format!("while ({cond}) {{\n");
        for st in &self.body {
            r.push_str(&format!("\t{st}\n"));
        }
        write!(f, "{r}}}")
    }
}

#[derive(Clone)]
pub struct BreakStmt {
    pub comments: Vec<String>,
}

impl fmt::Display for BreakStmt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "break")
    }
}

#[derive(Clone)]
pub struct ContinueStmt {
    pub comments: Vec<String>,
}

impl fmt::Display for ContinueStmt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "continue")
    }
}

#[derive(Clone)]
pub struct ScopeStmt {
    pub comments: Vec<String>,
    pub label: String,
    pub body: Vec<Stmt>,
}

impl fmt::Display for ScopeStmt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut r = format!("{}: {{\n", self.label);
        for st in &self.body {
            r.push_str(&format!("\t{st}\n"));
        }
        write!(f, "{r}}}")
    }
}

// ── File ─────────────────────────────────────────────────────────────

#[derive(Clone, Default)]
pub struct File {
    pub package: String,
    pub rwir_decls: Vec<RwirDecl>,
    pub funcs: Vec<Func>,
    pub top_level_calls: Vec<Instruction>,
    pub init_body: Vec<Stmt>,
}

/// 包树节点：按 lib 包名分组，format 时重建嵌套 lib 块（保包名，round-trip 不丢）。
#[derive(Default)]
struct PkgNode {
    rwirs: Vec<RwirDecl>,
    funcs: Vec<Func>,
    body: Vec<Stmt>,
    children: std::collections::BTreeMap<String, PkgNode>,
}

fn pkg_node<'a>(root: &'a mut PkgNode, pkg: &str) -> &'a mut PkgNode {
    if pkg.is_empty() {
        return root;
    }
    let mut cur = root;
    for seg in pkg.split('/') {
        cur = cur.children.entry(seg.to_string()).or_default();
    }
    cur
}

fn emit_node(sb: &mut String, node: &PkgNode, indent: &str) {
    let mut items: Vec<String> = Vec::new();
    for d in &node.rwirs {
        let mut s = String::new();
        for c in &d.comments {
            s.push_str(indent);
            s.push_str(c);
            s.push('\n');
        }
        s.push_str(indent);
        s.push_str(&d.sig_string());
        items.push(s);
    }
    for f in &node.funcs {
        let mut s = String::new();
        for c in &f.comments {
            s.push_str(indent);
            s.push_str(c);
            s.push('\n');
        }
        s.push_str(indent);
        s.push_str(&f.sig.to_string());
        s.push_str(" {\n");
        format_body(&mut s, &f.body, &format!("{indent}\t"));
        s.push_str(indent);
        s.push('}');
        items.push(s);
    }
    if !node.body.is_empty() {
        let mut s = String::new();
        format_body(&mut s, &node.body, indent);
        items.push(s.trim_end_matches('\n').to_string());
    }
    for (name, child) in &node.children {
        let mut s = String::new();
        s.push_str(indent);
        s.push_str("lib ");
        s.push_str(name);
        s.push_str(" {\n");
        emit_node(&mut s, child, &format!("{indent}\t"));
        s.push_str(indent);
        s.push('}');
        items.push(s);
    }
    sb.push_str(&items.join("\n\n"));
}

impl File {
    /// 格式化为规范 kvlang 源码（重建 lib 分组，保留包名，round-trip 语义等价）。
    pub fn format(&self) -> String {
        let mut root = PkgNode::default();
        for d in &self.rwir_decls {
            pkg_node(&mut root, &d.pkg).rwirs.push(d.clone());
        }
        for f in &self.funcs {
            let n = pkg_node(&mut root, &f.pkg);
            if f.sig.name == "init" {
                n.body.extend(f.body.clone());
            } else {
                n.funcs.push(f.clone());
            }
        }
        let mut sb = String::new();
        emit_node(&mut sb, &root, "");
        for inst in &self.top_level_calls {
            if !sb.is_empty() {
                sb.push('\n');
            }
            for c in &inst.comments {
                sb.push_str(c);
                sb.push('\n');
            }
            sb.push_str(&inst.to_string());
        }
        sb
    }
}

/// 缩进格式化语句体（对齐 Go ast.formatBody）。
fn format_body(sb: &mut String, stmts: &[Stmt], indent: &str) {
    for (i, st) in stmts.iter().enumerate() {
        if i > 0 {
            let prev = &stmts[i - 1];
            let prev_block = matches!(prev, Stmt::Scope(_) | Stmt::If(_));
            let cur_block = matches!(st, Stmt::Scope(_) | Stmt::If(_));
            if prev_block || cur_block {
                sb.push('\n');
            }
        }
        for c in stmt_comments(st) {
            sb.push_str(indent);
            sb.push_str(c);
            sb.push('\n');
        }
        let child = format!("{indent}\t");
        match st {
            Stmt::Instruction(s) => {
                sb.push_str(indent);
                sb.push_str(&s.to_string());
                sb.push('\n');
            }
            Stmt::Scope(s) => {
                sb.push_str(indent);
                sb.push_str(&s.label);
                sb.push_str(": {\n");
                format_body(sb, &s.body, &child);
                sb.push_str(indent);
                sb.push_str("}\n");
            }
            Stmt::If(s) => {
                let cond = s.cond.as_ref().map(|c| c.to_string()).unwrap_or_default();
                sb.push_str(indent);
                sb.push_str(&format!("if ({cond}) {{\n"));
                format_body(sb, &s.then_, &child);
                if !s.else_.is_empty() {
                    sb.push_str(indent);
                    sb.push_str("} else {\n");
                    format_body(sb, &s.else_, &child);
                }
                sb.push_str(indent);
                sb.push_str("}\n");
            }
            Stmt::For(s) => {
                sb.push_str(indent);
                sb.push_str(&format!("for ({} in {}) {{\n", s.var, s.iter));
                format_body(sb, &s.body, &child);
                sb.push_str(indent);
                sb.push_str("}\n");
            }
            Stmt::While(s) => {
                let cond = s.cond.as_ref().map(|c| c.to_string()).unwrap_or_default();
                sb.push_str(indent);
                sb.push_str(&format!("while ({cond}) {{\n"));
                format_body(sb, &s.body, &child);
                sb.push_str(indent);
                sb.push_str("}\n");
            }
            Stmt::Break(_) => {
                sb.push_str(indent);
                sb.push_str("break\n");
            }
            Stmt::Continue(_) => {
                sb.push_str(indent);
                sb.push_str("continue\n");
            }
        }
    }
}

// ── 工具 ─────────────────────────────────────────────────────────────

fn escape_string(s: &str) -> String {
    let mut b = String::new();
    for c in s.bytes() {
        match c {
            b'\\' => b.push_str("\\\\"),
            b'"' => b.push_str("\\\""),
            b'\n' => b.push_str("\\n"),
            b'\t' => b.push_str("\\t"),
            b'\r' => b.push_str("\\r"),
            _ => b.push(c as char),
        }
    }
    b
}

fn is_operator_char(c: u8) -> bool {
    symbol::scanner_one_char_ops().contains(&c)
}
