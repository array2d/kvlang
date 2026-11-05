//! 语法分析（对齐 parser/parser.go、inst.go、stmt.go）：Token 流 → AST。
//!
//! 入口：`parse_code(src) → Result<(File, Vec<Diagnostic>), String>`。

use super::ast::{
    self, Expr, Field, Func, FuncSig, Instruction, MEMBER_READ_OP, Param, RwirDecl, Stmt,
    StructDecl,
};
use super::keytree;
use super::scanner::{scan_with_diags, Diagnostic, Kind, Pos, Token};
use super::symbol;

pub fn has_errors(diags: &[Diagnostic]) -> bool {
    diags.iter().any(|d| !d.warn && !d.info)
}

/// 从源码字符串解析为 ast::File。
pub fn parse_code(src: &str) -> Result<(ast::File, Vec<Diagnostic>), String> {
    if src.trim().is_empty() {
        return Err("empty input".to_string());
    }
    let lines: Vec<String> = src.split('\n').map(|s| s.to_string()).collect();
    let (tokens, lex_errors) = scan_with_diags(src);
    let mut p = Parser {
        tokens,
        pos: 0,
        errors: lex_errors,
        ptr_params: Vec::new(),
        value_params: Vec::new(),
        flow: FlowCtx::default(),
    };
    let f = p.parse_file();
    for d in &mut p.errors {
        if d.pos.line > 0 && (d.pos.line as usize) <= lines.len() {
            d.source = lines[(d.pos.line - 1) as usize].clone();
        }
        d.src_name = "<inline>".to_string();
    }
    Ok((f, p.errors))
}

// ── parser 结构体 ─────────────────────────────────────────────────────

/// 签名出现在哪种定义处。两者文法相同，但**参数多态的能力不同**：
///
/// - `rwfunc`：参数与返回值必须是**单一类型**（不得含并集）——调用方只能从声明推类型；
/// - `rwir` 声明：允许并集，算子由编译期特化器按实参类型收敛成唯一 opid。
///
/// 见 `stdlib/kvlang/spec/03-类型系统/05-langtype签名类型表达式.kv`。
#[derive(Clone, Copy, PartialEq, Eq)]
enum SignatureSite {
    Rwfunc,
    RwDeclaration,
}

struct Parser {
    tokens: Vec<Token>,
    pos: usize,
    errors: Vec<Diagnostic>,
    // 当前函数体的指针形参名（声明带 `*`/`@`）。新参数模型下形参名就是**指针**：
    // 读值要写 `*p`，做成员/坐标访问要写 `(*p)·x`——`·`/`[` 直接作用在 `p` 上是
    // 隐式解引用，一律报错（spec 05-runtime语义/04-函数调用「禁止隐式解引用」）。
    ptr_params: Vec<String>,
    // 当前函数体的**值复制**形参名（声明无 `*`/`@` 且类型是标量）。体内只读，
    // 不能被解引用、不能作写槽。
    value_params: Vec<String>,
    // Per-function control-flow context, validated at function end.
    flow: FlowCtx,
}

/// Per-function control-flow state: loop depth (`break`/`continue`), block labels, and
/// `goto`/`br` jumps (validated in the parser to report source positions).
#[derive(Default)]
struct FlowCtx {
    loop_depth: u32,
    labels: Vec<(String, Pos)>,
    jumps: Vec<(Expr, Pos)>,
}

impl Parser {
    fn error_at(&mut self, pos: Pos, message: String) {
        self.errors.push(Diagnostic {
            pos,
            message,
            warn: false,
            info: false,
            source: String::new(),
            src_file: String::new(),
            src_name: String::new(),
        });
    }

    /// Parse one comma-separated list item; if no token is consumed (e.g. `=` in `f(a=1)`),
    /// emit an error and advance one token to prevent infinite loops.
    fn parse_list_item(&mut self, items: &mut Vec<Expr>, ctx: &str) {
        let before = self.pos;
        if let Some(e) = self.parse_pratt(0) {
            items.push(e);
        }
        if self.pos == before {
            let t = self.advance();
            self.error_at(
                t.pos,
                format!("unexpected {} {:?} in {ctx}", t.kind, t.value),
            );
        }
    }

    /// Finish a function body: validate label uniqueness and `goto`/`br` targets/arity, then restore `outer`.
    fn finish_flow(&mut self, outer: FlowCtx) {
        let flow = std::mem::replace(&mut self.flow, outer);
        let mut defined: std::collections::HashMap<String, Pos> = std::collections::HashMap::new();
        for (label, pos) in flow.labels {
            if let Some(first) = defined.get(&label) {
                let msg = format!(
                    "duplicate block label {label:?} (first defined at {}:{})",
                    first.line, first.col
                );
                self.error_at(pos, msg);
            } else {
                defined.insert(label, pos);
            }
        }
        for (e, pos) in flow.jumps {
            let (want, label_args): (usize, &[usize]) = if e.op == "goto" {
                (1, &[0])
            } else {
                (3, &[1, 2])
            };
            if e.args.len() != want {
                let shape = if want == 1 {
                    "goto(label)"
                } else {
                    "br(cond, then_label, else_label)"
                };
                self.error_at(
                    pos,
                    format!("{} expects {want} argument(s) — {shape}; got {}", e.op, e.args.len()),
                );
                continue;
            }
            for &i in label_args {
                let a = &e.args[i];
                if !a.is_leaf() || a.quote != 0 || !defined.contains_key(&a.val) {
                    let shown = if a.is_leaf() { a.val.clone() } else { a.to_string() };
                    self.error_at(
                        pos,
                        format!(
                            "{}: unknown label {shown:?} — target must be a block label `name: {{ … }}` defined in this function",
                            e.op
                        ),
                    );
                }
            }
        }
    }

    fn peek(&self) -> Token {
        self.tokens
            .get(self.pos)
            .cloned()
            .unwrap_or_else(|| Token::eof(Pos { line: 0, col: 0 }))
    }

    fn peek_at(&self, offset: isize) -> Token {
        let idx = self.pos as isize + offset;
        if idx < 0 || idx >= self.tokens.len() as isize {
            return Token::eof(Pos { line: 0, col: 0 });
        }
        self.tokens[idx as usize].clone()
    }

    fn advance(&mut self) -> Token {
        let t = self.peek();
        if self.pos < self.tokens.len() {
            self.pos += 1;
        }
        t
    }

    fn eat(&mut self, k: Kind) -> bool {
        if self.peek().kind == k {
            self.advance();
            true
        } else {
            false
        }
    }

    fn expect(&mut self, k: Kind) -> Token {
        let t = self.advance();
        if t.kind != k {
            self.errors.push(Diagnostic {
                pos: t.pos,
                message: format!("expected {k}, got {} {:?}", t.kind, t.value),
                warn: false,
                info: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
            return Token {
                kind: k,
                value: String::new(),
                pos: t.pos,
                quote: 0,
            };
        }
        t
    }

    fn skip_newlines(&mut self) {
        while self.peek().kind == Kind::Newline {
            self.advance();
        }
    }

    fn skip_newlines_and_comments(&mut self) {
        loop {
            let k = self.peek().kind;
            if k == Kind::Newline || k == Kind::Comment {
                self.advance();
            } else {
                break;
            }
        }
    }

    fn collect_leading_comments(&mut self) -> Vec<String> {
        let mut comments = Vec::new();
        loop {
            match self.peek().kind {
                Kind::Newline => {
                    self.advance();
                }
                Kind::Comment => comments.push(self.advance().value),
                _ => return comments,
            }
        }
    }

    // ── 文件级解析 ─────────────────────────────────────────────────

    fn parse_file(&mut self) -> ast::File {
        let mut f = ast::File::default();
        loop {
            let comments = self.collect_leading_comments();
            if self.peek().kind == Kind::EOF {
                break;
            }

            let is_lib = self.peek().kind == Kind::Ident
                && self.peek().value == "lib"
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(2).kind == Kind::LBrace;
            if is_lib {
                self.parse_lib_body(&mut f, "");
                continue;
            }

            let is_struct = self.peek().kind == Kind::Ident
                && self.peek().value == "struct"
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(2).kind == Kind::LBrace;
            if is_struct {
                let mut decl = self.parse_struct_decl();
                decl.comments = comments;
                f.structs.push(decl);
                continue;
            }

            if self.peek().kind == Kind::Ident && self.peek().value == "rwir" {
                let decl = self.parse_rwir_decl();
                f.rwir_decls.push(decl);
            } else if self.peek().kind == Kind::Ident && self.peek().value == "rwfunc" {
                if f.package.is_empty() {
                    let pos = self.peek().pos;
                    self.errors.push(Diagnostic {
                        pos,
                        message: "rwfunc outside lib block — registering under /lib/<name>; consider wrapping in 'lib pkgname { }'".to_string(),
                        info: true,
                        warn: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                let mut func = self.parse_func();
                func.comments = comments;
                if f.package.is_empty() && symbol::by_word(&func.sig.name).word == func.sig.name {
                    let pos = self.peek().pos;
                    self.errors.push(Diagnostic {
                        pos,
                        message: format!("function {:?} shadows builtin {:?} — wrap in 'lib pkg {{ }}' or rename", func.sig.name, func.sig.name),
                        warn: false,
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                f.funcs.push(func);
            } else if matches!(self.peek().kind, Kind::If | Kind::While | Kind::For) {
                let t = self.peek();
                let lower = t.kind.to_str().to_lowercase();
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    message: format!("top-level {lower} is not supported — wrap in main()"),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                self.advance();
            } else {
                let prev_pos = self.pos;
                let insts = self.parse_inst();
                if !insts.is_empty() {
                    for (k, mut inst) in insts.into_iter().enumerate() {
                        if inst.expr.is_some() {
                            if k == 0 {
                                inst.comments = comments.clone();
                            }
                            f.top_level_calls.push(inst);
                        }
                    }
                } else if self.pos == prev_pos {
                    if self.peek().kind != Kind::EOF {
                        let t = self.peek();
                        self.errors.push(Diagnostic {
                            pos: t.pos,
                            message: format!(
                                "unexpected token {} {:?} at top level",
                                t.kind, t.value
                            ),
                            warn: false,
                            info: false,
                            source: String::new(),
                            src_file: String::new(),
                            src_name: String::new(),
                        });
                        self.advance();
                    }
                }
            }
        }
        // Validate `goto`/`br` targets in the synthesized top-level `init` body.
        self.finish_flow(FlowCtx::default());
        f
    }

    fn parse_lib_body(&mut self, f: &mut ast::File, prefix: &str) {
        self.advance(); // consume "lib"
        let name = self.advance().value;
        let pkg = if prefix.is_empty() {
            name.clone()
        } else {
            format!("{prefix}/{name}")
        };
        if name == "lib" && prefix.is_empty() {
            let pos = self.peek().pos;
            self.errors.push(Diagnostic {
                pos,
                message: format!(
                    "package name {name:?} expands to /lib/lib/ — consider a different name"
                ),
                info: true,
                warn: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        let prev_pkg = f.package.clone();
        f.package = pkg.clone();
        self.expect(Kind::LBrace);
        // Bare statements inside a `lib` block form an independent `init` control-flow context.
        let outer_flow = std::mem::take(&mut self.flow);
        let mut body: Vec<Stmt> = Vec::new();
        loop {
            let mut comments = self.collect_leading_comments();
            if self.peek().kind == Kind::RBrace || self.peek().kind == Kind::EOF {
                break;
            }
            let is_nested_lib = self.peek().kind == Kind::Ident
                && self.peek().value == "lib"
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(2).kind == Kind::LBrace;
            if is_nested_lib {
                self.parse_lib_body(f, &pkg);
                continue;
            }
            let is_struct = self.peek().kind == Kind::Ident
                && self.peek().value == "struct"
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(2).kind == Kind::LBrace;
            if is_struct {
                let mut decl = self.parse_struct_decl();
                decl.pkg = pkg.clone();
                decl.comments = comments;
                f.structs.push(decl);
                continue;
            }
            if self.peek().kind == Kind::Ident && self.peek().value == "rwir" {
                let mut decl = self.parse_rwir_decl();
                decl.pkg = pkg.clone();
                decl.comments = comments;
                f.rwir_decls.push(decl);
                continue;
            } else if self.peek().kind == Kind::Ident && self.peek().value == "rwfunc" {
                let mut func = self.parse_func();
                func.pkg = pkg.clone();
                func.comments = comments;
                f.funcs.push(func);
                continue;
            }
            let sts = self.parse_stmt();
            if sts.is_empty() {
                break;
            }
            for st in sts {
                let cs = std::mem::take(&mut comments);
                body.push(attach_comments(st, cs));
            }
        }
        self.expect(Kind::RBrace);
        self.finish_flow(outer_flow);
        if !body.is_empty() {
            f.funcs.push(Func {
                comments: Vec::new(),
                sig: FuncSig {
                    name: "init".to_string(),
                    params: Vec::new(),
                    returns: Vec::new(),
                },
                body,
                pkg: pkg.clone(),
            });
        }
        f.package = prev_pkg;
    }

    fn parse_func(&mut self) -> Func {
        let sig = self.parse_func_sig();
        self.check_param_types(&sig, SignatureSite::Rwfunc);
        self.check_variadic(&sig);
        self.check_param_dup(&sig);
        // 指针参数的判据：显式 `*`/`@` 前缀（`by_ref`），或类型本身是值容器
        // （structref `/lib/X`、mapexpr `k·v`——容器没有可拷贝的值，只能按地址传）。
        self.ptr_params = sig
            .params
            .iter()
            .chain(sig.returns.iter())
            .filter(|p| {
                !p.by_ref.is_empty() || p.ty.starts_with('/') || p.ty.contains('\u{b7}')
            })
            .map(|p| p.name.clone())
            .collect();
        self.value_params = sig
            .params
            .iter()
            .filter(|p| p.by_ref.is_empty())
            .map(|p| p.name.clone())
            .collect();
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let outer_flow = std::mem::take(&mut self.flow);
        let body = self.parse_body();
        self.expect(Kind::RBrace);
        self.finish_flow(outer_flow);
        self.ptr_params.clear();
        self.value_params.clear();
        let func = Func {
            comments: Vec::new(),
            sig,
            body,
            pkg: String::new(),
        };
        self.check_read_only_params(&func);
        func
    }

    fn parse_rwir_decl(&mut self) -> RwirDecl {
        self.advance(); // consume 'rwir'
        let mut decl = RwirDecl {
            comments: Vec::new(),
            sig: FuncSig {
                name: String::new(),
                params: Vec::new(),
                returns: Vec::new(),
            },
            pkg: String::new(),
        };
        if self.peek().kind == Kind::Ident {
            decl.sig.name = self.advance().value;
            if self.peek().kind == Kind::Dot {
                self.advance(); // .
                decl.sig.name.push_str(keytree::MEMBER_SEP);
                if self.peek().kind == Kind::Ident {
                    decl.sig.name.push_str(&self.advance().value);
                }
            }
        }
        if self.peek().kind == Kind::LParen {
            self.advance();
            decl.sig.params = self.parse_param_list(Kind::RParen);
            self.expect(Kind::RParen);
        }
        if self.peek().kind == Kind::Arrow {
            self.advance();
            self.skip_newlines();
            if self.peek().kind == Kind::LParen {
                self.advance();
                decl.sig.returns = self.parse_param_list(Kind::RParen);
                self.expect(Kind::RParen);
            }
        }
        self.check_param_types(&decl.sig, SignatureSite::RwDeclaration);
        self.check_variadic(&decl.sig);
        self.check_param_dup(&decl.sig);
        decl
    }

    fn parse_struct_decl(&mut self) -> StructDecl {
        self.advance(); // consume 'struct'
        let name = self.advance().value; // struct 名
        self.expect(Kind::LBrace);
        let mut fields = Vec::new();
        loop {
            while matches!(
                self.peek().kind,
                Kind::Newline | Kind::Comma | Kind::Comment
            ) {
                self.advance();
            }
            if self.peek().kind == Kind::RBrace || self.peek().kind == Kind::EOF {
                break;
            }
            if self.peek().kind != Kind::Ident {
                let t = self.peek();
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    message: format!("struct {name:?}: expected field name, got {:?}", t.value),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                break;
            }
            let fname = self.advance().value;
            let mut ty = String::new();
            if self.peek().kind == Kind::Colon {
                self.advance();
                ty = self.parse_type();
                if ty.is_empty() {
                    self.errors.push(Diagnostic {
                        pos: Pos { line: 0, col: 0 },
                        message: format!(
                            "struct field {fname:?}: empty kindexp is only allowed in                              signature slots (`name:`)"
                        ),
                        warn: false,
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
            }
            let mut default = None;
            if self.peek().kind == Kind::Arrow && self.peek().value == "=" {
                self.advance();
                let pos = self.peek().pos;
                default = self.parse_pratt(0);
                // 字段默认值同样是字面量给的类型（`f:int=0` 的 `0` 即 int64），标注须是已知种类名。
                if default.as_ref().is_some_and(expr_is_scalar_lit)
                    && !super::langtype::is_plain_kind(&ty)
                {
                    self.push_unknown_type("struct field", &fname, &ty, pos);
                }
                if let Some(d) = &default {
                    for mut diag in super::lower::check_field_default(&fname, &ty, d) {
                        diag.pos = pos;
                        self.errors.push(diag);
                    }
                }
            }
            fields.push(Field {
                name: fname,
                ty,
                default,
            });
        }
        self.expect(Kind::RBrace);
        StructDecl {
            comments: Vec::new(),
            name,
            pkg: String::new(),
            fields,
        }
    }

    fn parse_func_sig(&mut self) -> FuncSig {
        self.advance(); // consume 'rwfunc'
        let mut sig = FuncSig {
            name: String::new(),
            params: Vec::new(),
            returns: Vec::new(),
        };
        if self.peek().kind == Kind::Ident {
            sig.name = self.advance().value;
        }
        if self.peek().kind == Kind::LParen {
            self.advance();
            sig.params = self.parse_param_list(Kind::RParen);
            self.expect(Kind::RParen);
        }
        if self.peek().kind == Kind::Arrow {
            self.advance();
            self.skip_newlines();
            if self.peek().kind == Kind::LParen {
                self.advance();
                sig.returns = self.parse_param_list(Kind::RParen);
                self.expect(Kind::RParen);
            } else {
                sig.returns = self.parse_param_list(Kind::LBrace);
            }
        }
        sig
    }

    fn parse_type(&mut self) -> String {
        let raw = self.parse_type_raw();
        if type_has_any(&raw) {
            self.errors.push(Diagnostic {
                pos: Pos { line: 0, col: 0 },
                message: "unknown type \"any\" — `any` 不再是类型名；不约束请写空 kindexp：                          签名槽 `name:`、Ptr 目标 `*`、mapexpr value `[k]·`"
                    .to_string(),
                warn: false,
                info: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
            return String::new();
        }
        super::langtype::expand_struct_refs(&raw)
    }

    fn parse_type_raw(&mut self) -> String {
        let mut sb = String::new();
        let mut depth = 0i32;
        loop {
            let t = self.peek();
            match t.kind {
                Kind::LBrack => {
                    depth += 1;
                    sb.push('[');
                    self.advance();
                }
                Kind::RBrack => {
                    depth -= 1;
                    sb.push(']');
                    self.advance();
                }
                // 括号分组：`*(int64|float64)` 里 `*` 作用于整个并集，括住才无歧义。
                // 只有类型**内部**的 `(` 才收（`depth > 0`——此时已在 `[` 内或已进入一层括号）。
                Kind::LParen => {
                    depth += 1;
                    sb.push('(');
                    self.advance();
                }
                Kind::RParen if depth > 0 => {
                    depth -= 1;
                    sb.push(')');
                    self.advance();
                }
                // `$name` 是参数声明的名字标记，不属于类型：遇到就收尾，留给调用方剥 `$`。
                Kind::Ident | Kind::Literal if t.value.starts_with('$') => return sb,
                Kind::Ident | Kind::Literal => {
                    sb.push_str(&t.value);
                    self.advance();
                }
                Kind::Comma => {
                    if depth > 0 {
                        sb.push(',');
                        self.advance();
                    } else {
                        return sb;
                    }
                }
                Kind::Dot if depth == 0 => {
                    // 单 `·`：stringkeymap 的 key·value 分隔（可嵌套）；三点 `...`：尾缀变参。
                    let mut dots = 0;
                    while self.peek().kind == Kind::Dot {
                        self.advance();
                        dots += 1;
                    }
                    if dots == 1 {
                        sb.push('·');
                        continue;
                    }
                    if dots == 3 {
                        sb.push_str("...");
                    }
                    return sb;
                }
                _ => return sb,
            }
        }
    }

    /// 参数声明只有两种形态（spec 04-layout语义/03-函数）：`*T $name` 传址、
    /// `T $name` 标量值复制。**类型在前、`$name` 在后**；`$` 只是声明的标记，
    /// 解析后剥掉——函数体内的引用写裸名（指针参数写 `*name`）。
    /// 旧写法 `name: type` 不再接受。
    fn parse_param_list(&mut self, stop: Kind) -> Vec<Param> {
        let mut params = Vec::new();
        while self.peek().kind != stop && self.peek().kind != Kind::EOF {
            self.skip_newlines();
            if self.peek().kind == stop {
                break;
            }
            if self.eat(Kind::Comma) {
                continue;
            }
            let start = self.peek();
            // 类型可以以 `[` 起头（`[]char/utf32`、`[int64]·int64`），也可以带 `*`/`@` 前缀。
            if !matches!(
                start.kind,
                Kind::Ident | Kind::Literal | Kind::LBrack
            ) {
                break;
            }
            // 旧写法：`name: type`——第一个词后跟冒号。
            if self.peek_at(1).kind == Kind::Colon {
                self.advance(); // name
                self.advance(); // :
                let ty = self.parse_type();
                self.errors.push(Diagnostic {
                    pos: start.pos,
                    warn: false,
                    info: false,
                    message: format!(
                        "参数声明必须是 `*T $name` 或 `T $name`；旧写法 `{}:{}` 不再接受",
                        start.value, ty
                    ),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                params.push(Param {
                    name: start.value,
                    ty,
                    by_ref: String::new(),
                    has_ty: true,
                });
                continue;
            }
            // 新写法：`*T $name` / `T $name`——类型在前。`$` 不是分隔符，故
            // `$name` 整个是一个 Ident；剥掉 `$` 即得体内引用的裸名。
            let ty = self.parse_type();
            let t = self.peek();
            let name = match t.value.strip_prefix('$').filter(|_| t.kind == Kind::Ident) {
                Some(n) => {
                    self.advance();
                    n.to_string()
                }
                None => {
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: false,
                        info: false,
                        message: format!(
                            "参数声明必须是 `*T $name` 或 `T $name`；类型 {ty:?} 后面缺少 `$name`"
                        ),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                    String::new()
                }
            };
            // 传递方式（`*`/`@`）与类型分开存：`ty` 只留类型表达式，
            // `*int64|float64` 的 `*` 作用于整个 union，剥掉后是 `int64|float64`。
            let by_ref = ty.chars().take_while(|c| *c == '*' || *c == '@').collect::<String>();
            let ty_value = ty.trim_start_matches(['*', '@']).to_string();
            // **参数必须声明类型**（spec 05-runtime语义/04-函数调用:27：
            // 「`$a`、`* $a`、`a:` 都是错误」）。空 kindexp 在**值类型**层面合法
            // （Ptr 目标可空），但参数声明是绑定、不是值类型表达式——没有类型就没有
            // 可绑定的契约，`$a` 与 `* $a` 都是缺注解。
            if ty_value.is_empty() {
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: false,
                    info: false,
                    message: format!("参数必须声明类型：`{name}` 的类型为空"),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
            params.push(Param {
                name,
                ty: ty_value,
                by_ref,
                has_ty: true,
            });
        }
        params
    }

    fn check_param_types(&mut self, sig: &FuncSig, site: SignatureSite) {
        // 值容器类型必须写 `*`：map langtype（含 `·`）与 structref（`/` 开头）的成员落在兄弟槽
        // `{key}·`，单槽不是完备值，没有可拷贝的"值"，只能按地址传。
        //
        // 注意 `*` **不切换**传递方式——kvlang 只有传址，不存在按值传（spec 03-类型系统/16-ptr
        // 末段、05-runtime语义/04-函数调用）。这条校验只是要求作者把值容器显式标注出来。
        //
        // 判据是 `is_value_container_ty`：**以 `/` 开头**（structref）或**含 `·`**（mapexpr）。
        // 裸名不是种类名时，`langtype::expand_struct_refs` 会先把它展开成 `/lib/<名字>`
        // （这是 struct 引用的语法糖），因此「写错/写漏种类名」的表现就是它突然被当成
        // structref 容器、要求加 `*`。遇到这条诊断而类型看着像标量时，先看它是不是
        // 拼错或未登记的种类名，而不是急着加 `*`。
        // `*` 作用于**整个类型表达式**，不能逐成员写：`*int64|float64` 合法
        // （指向 int64|float64 的指针），`*int64|*float64` 非法（同一参数两种传递方式）。
        // `*` 是传递方式不是类型的一部分，逐成员写就是把它当类型用了（spec [[ptr]]）。
        for param in sig.params.iter().chain(sig.returns.iter()) {
            let members: Vec<&str> = param.ty.split('|').collect();
            if members.len() > 1 && members.iter().skip(1).any(|m| m.trim().starts_with('*')) {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    warn: false,
                    info: false,
                    message: format!(
                        "参数 {:?}: `*` 作用于整个类型表达式，不能逐成员写 —— 写 `{}`",
                        param.name, {
                            let stripped: Vec<&str> =
                                members.iter().map(|m| m.trim().trim_start_matches('*')).collect();
                            format!("*{} ${}", stripped.join("|"), param.name)
                        }
                    ),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        // 参数位置与返回值位置各发**一条**诊断，不并列两个事实源：
        // 参数侧的判据是「值复制只允许哪些类型」，返回值侧是「容器没有可拷贝的值」。
        for param in &sig.params {
            if !is_value_container_ty(&param.ty) || !param.by_ref.is_empty() {
                continue;
            }
            let pointer_form = param
                .ty
                .split('|')
                .map(|a| format!("*{a}"))
                .collect::<Vec<_>>()
                .join("|");
            self.errors.push(Diagnostic {
                pos: Pos { line: 0, col: 0 },
                message: format!(
                    "func {}: param {:?}: 值复制参数只能是字符串、数值或 bool —— 容器请写 `{} ${}`",
                    sig.name, param.name, pointer_form, param.name
                ),
                warn: false,
                info: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        for ret in &sig.returns {
            if is_value_container_ty(&ret.ty) && ret.by_ref.is_empty() {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!(
                        "func {}: return value {:?}: 值容器类型只能按地址传递 —— 写 `{:?}`",
                        sig.name,
                        ret.name,
                        ret.ty
                            .split('|')
                            .map(|a| {
                                if !a.starts_with('*') && (a.starts_with('/') || a.contains('·')) {
                                    format!("*{a}")
                                } else {
                                    a.to_string()
                                }
                            })
                            .collect::<Vec<_>>()
                            .join("|")
                    ),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        for param in &sig.params {
            // 末读参 `...` 是签名层变参标记，校验前剥离（变参落 dynamic 字节，见 [[函数]]）。
            let ty = param.ty.strip_suffix("...").unwrap_or(&param.ty);
            if !crate::langtype::valid_langtype(ty) {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!(
                        "func {}: param {:?}: {} (got {:?})",
                        sig.name,
                        param.name,
                        type_error(&param.ty),
                        param.ty
                    ),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            } else if site == SignatureSite::Rwfunc && crate::langtype::has_union(ty) {
                // rwfunc 的参数是帧槽的**唯一**类型：并集会让调用方无从推出实参类型。
                // rwir 算子的并集由编译期特化器收敛，不受此约束（见 [[langtype签名类型表达式]]）。
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!(
                        "func {}: param {:?}: rwfunc parameter must be a single type, \
                         a union is not allowed (got {:?})",
                        sig.name, param.name, param.ty
                    ),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        for ret in &sig.returns {
            // 变参标记剥离后校验；变参写参的非法性由 check_variadic 专门报错。
            let ty = ret.ty.strip_suffix("...").unwrap_or(&ret.ty);
            if !crate::langtype::valid_langtype(ty) {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!(
                        "func {}: return value {:?}: {} (got {:?})",
                        sig.name,
                        ret.name,
                        type_error(&ret.ty),
                        ret.ty
                    ),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            } else if site == SignatureSite::Rwfunc && crate::langtype::has_union(ty) {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!(
                        "func {}: return value {:?}: rwfunc return type must be a single type, \
                         a union is not allowed (got {:?})",
                        sig.name, ret.name, ret.ty
                    ),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        for param in &sig.params {
            if !param.has_ty {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("func {}: param {:?} has no type annotation — every parameter must declare its type (e.g. {}:int64) or explicitly write {}: for unconstrained (empty kindexp)", sig.name, param.name, param.name, param.name),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        for ret in &sig.returns {
            if !ret.has_ty {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("func {}: return value {:?} has no type annotation — every return value must declare its type (e.g. {}:int64) or explicitly write {}: for unconstrained (empty kindexp)", sig.name, ret.name, ret.name, ret.name),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
    }

    fn check_variadic(&mut self, sig: &FuncSig) {
        let last = sig.params.len().saturating_sub(1);
        for (i, p) in sig.params.iter().enumerate() {
            if p.ty.ends_with("...") && i != last {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!(
                        "func {}: variadic param {:?} must be the last read-param",
                        sig.name, p.name
                    ),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        for r in &sig.returns {
            if r.ty.ends_with("...") {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!(
                        "func {}: write-param {:?} cannot be variadic",
                        sig.name, r.name
                    ),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
    }

    /// 参数名在函数内**全局唯一**（见 [[函数]] 的参数同名规则）：读参列表内、写参列表内、读写之间
    /// 均不得同名——变量名即指针，同名即同址。**调用**时不受限：同一变量可同时占读槽与写槽
    /// （`inc(x) -> x` 合法）。
    fn check_param_dup(&mut self, sig: &FuncSig) {
        let mut reads = std::collections::HashSet::new();
        let mut writes = std::collections::HashSet::new();
        let err = |p: &mut Self, msg: String| {
            p.errors.push(Diagnostic {
                pos: Pos { line: 0, col: 0 },
                message: msg,
                warn: false,
                info: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        };
        for p in &sig.params {
            if !reads.insert(p.name.as_str()) {
                err(self, format!("func {}: duplicate param {:?} in read-params — read-params, write-params and their union must all be name-unique (a name is an address)", sig.name, p.name));
            }
        }
        for r in &sig.returns {
            if reads.contains(r.name.as_str()) {
                err(self, format!("func {}: param {:?} appears in both read-params and write-params — a param is either read-only or write-only, pick one", sig.name, r.name));
            } else if !writes.insert(r.name.as_str()) {
                err(self, format!("func {}: duplicate param {:?} in write-params — read-params, write-params and their union must all be name-unique (a name is an address)", sig.name, r.name));
            }
        }
    }

    fn check_read_only_params(&mut self, func: &Func) {
        let mut ro = std::collections::HashSet::new();
        for p in func.sig.params.iter() {
            ro.insert(p.name.clone());
        }
        if ro.is_empty() {
            return;
        }
        let bad = |p: &mut Self, w: &str, fname: &str| {
            p.errors.push(Diagnostic {
                pos: Pos { line: 1, col: 1 },
                message: format!("func {fname}: read param {w:?} cannot be used as write slot (read params are read-only)"),
                warn: false,
                info: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        };
        // 别名污染：`q = p` / `p -> q`（p 是读参或已污染）使 q 指到**同一个对象**，
        // 于是经 q 写成员就是在写读参的对象。只读性沿别名传播，否则 `q = p; q·x = v`
        // 一句话就把「签名诚实原则」洗白了。
        // 别名 → 源头读参（`q = p` 记 q→p；再 `r = q` 追到 p），供诊断点名真凶。
        let mut tainted: std::collections::HashMap<String, String> =
            std::collections::HashMap::new();
        let mut all: Vec<&Instruction> = Vec::new();
        collect_body_insts(&func.body, &mut all);
        loop {
            let mut grew = false;
            for inst in &all {
                let Some(e) = &inst.expr else { continue };
                if !e.is_leaf() {
                    continue;
                }
                let src = if ro.contains(&e.val) {
                    e.val.clone()
                } else {
                    match tainted.get(&e.val) {
                        Some(s0) => s0.clone(),
                        None => continue,
                    }
                };
                for w in &inst.writes {
                    if w.contains('/') || w.contains('[') || w.contains(keytree::MEMBER_SEP) {
                        continue;
                    }
                    if !ro.contains(w) && !tainted.contains_key(w) {
                        tainted.insert(w.clone(), src.clone());
                        grew = true;
                    }
                }
            }
            if !grew {
                break;
            }
        }
        let check = |p: &mut Self,
                     inst: &Instruction,
                     ro: &std::collections::HashSet<String>,
                     fname: &str| {
            // 写槽命中读参（非路径/索引/成员）——把参数槽重绑成别的地址，拒绝。
            // 别名局部量不在此列：`cur·next -> cur` 重绑的是局部别名本身，没碰调用方对象。
            for w in inst.writes.iter() {
                if w.contains('/') || w.contains('[') || w.contains(keytree::MEMBER_SEP) {
                    continue;
                }
                if ro.contains(w) {
                    bad(p, w, fname);
                    continue;
                }
                // `*A = v` 写穿读参的 kvlang 指针——那改的是**调用方的数据**，
                // 与把读参放到写槽是同一种违约。只读性按**位置**分（`[0,-i]` 读 /
                // `[0,+i]` 写），不按 `*` 分：写穿不能绕过签名诚实原则。
                if let Some(base) = w.strip_prefix(ast::DEREF_OP) {
                    if ro.contains(base) {
                        bad(p, base, fname);
                    }
                }
            }
            // 成员写（`v -> base·a·b` / `v -> base·*key`）：base 在写槽里（签名诚实原则）。
            // 被写的参数**必须**声明在写参侧——要就地改调用方的对象，就把该参数写到
            // `-> (p:Point)` 里，而不是留在读参侧靠别名绕。
            if let Some(base) = inst
                .writes
                .iter()
                .find(|w| w.contains(keytree::MEMBER_SEP))
                .and_then(|w| w.split(keytree::MEMBER_SEP).next())
                .filter(|b| !b.contains('/'))
            {
                // `(*A)·x` 的基座写成 `(*A)`：剥括号再查只读表——写穿同样违约。
                let bare = base
                    .strip_prefix("(*")
                    .and_then(|r| r.strip_suffix(')'))
                    .unwrap_or(base);
                if ro.contains(bare) {
                    bad(p, bare, fname);
                } else if let Some(src) = tainted.get(base) {
                    let ty = func
                        .sig
                        .params
                        .iter()
                        .find(|pp| &pp.name == src)
                        .map(|pp| pp.ty.clone())
                        .unwrap_or_default();
                    p.errors.push(Diagnostic {
                        pos: Pos { line: 1, col: 1 },
                        message: format!(
                            "func {fname}: {base:?} aliases read param {src:?} (`{base} = {src}`); writing its members writes that value — put it on the write side: `-> ({src}:{ty})`"
                        ),
                        warn: false,
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
            }
        };
        let fname = func.sig.name.clone();
        walk_read_only(self, &func.body, &ro, &fname, &check);
    }

    // ── 语句级 ─────────────────────────────────────────────────────

    fn parse_body(&mut self) -> Vec<Stmt> {
        let mut stmts = Vec::new();
        loop {
            let mut comments = self.collect_leading_comments();
            let t = self.peek();
            if t.kind == Kind::RBrace || t.kind == Kind::EOF {
                break;
            }
            let before = self.pos;
            for st in self.parse_stmt() {
                let cs = std::mem::take(&mut comments);
                stmts.push(attach_comments(st, cs));
            }
            // panic-mode 恢复：一轮没消费任何 token（如错误恢复后游标停在 parse_primary_expr
            // 不消费即返回 None 的 token 上）——报诊断并跳过一个 token，保证前进性，杜绝死循环。
            if self.pos == before {
                let bad = self.advance();
                self.errors.push(Diagnostic {
                    pos: bad.pos,
                    message: format!("unexpected token {} {:?} in block", bad.kind, bad.value),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        stmts
    }

    /// 解析一条语句，返回**一或 N 条** Stmt（多赋值展开为多条指令，其余语句恒 1 条）。
    /// 空 Vec = 解析不出语句（调用方据此结束块 / break）。
    fn parse_stmt(&mut self) -> Vec<Stmt> {
        // 块标签检测（优先级最高）。排除类型注解写槽：`x:Type`（Ident）、`x:[…]`（LBrack）、
        // `x:/lib/Name`（structref langtype，以 / 起头的路径 Literal）。
        let t2 = self.peek_at(2);
        let t2_structref = t2.kind == Kind::Literal && t2.value.starts_with('/');
        if self.peek_at(1).kind == Kind::Colon
            && t2.kind != Kind::Ident
            && t2.kind != Kind::LBrack
            && !t2_structref
        {
            // `x: = …` 是空 kindexp 标注（不是标签）：只允许出现在签名槽。
            if t2.kind == Kind::Arrow && t2.value == "=" {
                self.errors.push(Diagnostic {
                    pos: self.peek().pos,
                    message: "empty kindexp is only allowed in signature slots (`name:`); \
                              变量标注必须写具体类型"
                        .to_string(),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                while self.peek().kind != Kind::Newline && self.peek().kind != Kind::EOF {
                    self.advance();
                }
                return Vec::new();
            }
            return vec![self.parse_block_label()];
        }
        match self.peek().kind {
            Kind::If => vec![self.parse_if()],
            Kind::For => vec![self.parse_for()],
            Kind::While => vec![self.parse_while()],
            Kind::Break | Kind::Continue => {
                let t = self.advance();
                self.eat(Kind::Newline);
                if self.flow.loop_depth == 0 {
                    // Still return the statement so `lib` block parsing continues; the recorded error blocks lowering.
                    let kw = if t.kind == Kind::Break { "break" } else { "continue" };
                    self.error_at(t.pos, format!("{kw} outside loop (only allowed inside while/for)"));
                }
                if t.kind == Kind::Break {
                    vec![Stmt::Break(ast::BreakStmt {
                        comments: Vec::new(),
                    })]
                } else {
                    vec![Stmt::Continue(ast::ContinueStmt {
                        comments: Vec::new(),
                    })]
                }
            }
            _ => self
                .parse_inst()
                .into_iter()
                .map(Stmt::Instruction)
                .collect(),
        }
    }

    fn parse_if(&mut self) -> Stmt {
        self.advance(); // consume 'if'
        let cond = self.parse_cond_inst();
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let then_ = self.parse_body();
        self.expect(Kind::RBrace);
        self.skip_newlines_and_comments();

        if self.peek().kind == Kind::Else && self.peek_at(1).kind == Kind::If {
            let els = self.parse_elif_chain();
            return Stmt::If(ast::IfStmt {
                comments: Vec::new(),
                cond,
                then_,
                else_: els,
            });
        }
        if self.peek().kind == Kind::Else {
            self.advance();
            self.skip_newlines_and_comments();
            self.expect(Kind::LBrace);
            let els = self.parse_body();
            self.expect(Kind::RBrace);
            return Stmt::If(ast::IfStmt {
                comments: Vec::new(),
                cond,
                then_,
                else_: els,
            });
        }
        Stmt::If(ast::IfStmt {
            comments: Vec::new(),
            cond,
            then_,
            else_: Vec::new(),
        })
    }

    fn parse_elif_chain(&mut self) -> Vec<Stmt> {
        self.advance(); // consume else
        self.advance(); // consume if
        let cond = self.parse_cond_inst();
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let body = self.parse_body();
        self.expect(Kind::RBrace);
        self.skip_newlines_and_comments();

        if self.peek().kind == Kind::Else && self.peek_at(1).kind == Kind::If {
            let chain = self.parse_elif_chain();
            return vec![Stmt::If(ast::IfStmt {
                comments: Vec::new(),
                cond,
                then_: body,
                else_: chain,
            })];
        }
        if self.peek().kind == Kind::Else {
            self.advance();
            self.skip_newlines_and_comments();
            self.expect(Kind::LBrace);
            let els = self.parse_body();
            self.expect(Kind::RBrace);
            return vec![Stmt::If(ast::IfStmt {
                comments: Vec::new(),
                cond,
                then_: body,
                else_: els,
            })];
        }
        vec![Stmt::If(ast::IfStmt {
            comments: Vec::new(),
            cond,
            then_: body,
            else_: Vec::new(),
        })]
    }

    fn parse_for(&mut self) -> Stmt {
        self.advance(); // consume 'for'
        self.expect(Kind::LParen);
        let mut var = String::new();
        let t = self.peek();
        if t.kind == Kind::Ident || t.kind == Kind::Literal {
            var = self.advance().value;
            if self.peek().kind == Kind::Colon {
                self.advance();
                if self.peek().kind == Kind::Ident {
                    self.advance(); // consume type（暂忽略）
                }
            }
        }
        if self.peek().kind == Kind::Ident && self.peek().value == "in" {
            self.advance();
        }
        let mut iter = ast::leaf("");
        if self.peek().kind != Kind::RParen && self.peek().kind != Kind::EOF {
            iter = self.parse_pratt(0).unwrap_or_else(|| ast::leaf(""));
        }
        // for-in 源允许顶层散 key 字面量 `for (x in {...})`（物化到临时槽后展开）。
        self.check_sparse_usage(&iter, true);
        self.expect(Kind::RParen);
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        self.flow.loop_depth += 1;
        let body = self.parse_body();
        self.flow.loop_depth -= 1;
        self.expect(Kind::RBrace);
        Stmt::For(ast::ForStmt {
            comments: Vec::new(),
            var,
            iter,
            body,
        })
    }

    /// structref 写目标上的容器字面量改写成 `struct·new(原型路径, …)`。
    ///
    /// 容器字面量只有 `map·new` 一种算子（groupkv 的构造），struct 实例化**不是**另一种
    /// 构造算子——它是同一个字面量在不同写目标下的落地方式，需要原型路径作为首参：
    /// 成员名来自原型，实例化即克隆原型子树再覆盖字段（见 spec [[struct]]）。
    ///
    /// 判具名用「首参是字符串字面量」：`{k=v …}` 的键由 parser 包成 `str_lit`，
    /// 位置型 `{v0, v1 …}` 的首参是值本身。这是解析期就确定的事实，不靠算子名区分。
    fn dispatch_struct_literal(&mut self, inst: &mut Instruction) {
        let is_named_map = inst.expr.as_ref().is_some_and(|e| {
            e.op == "map·new" && e.args.first().is_some_and(|a| a.quote != 0)
        });
        if !is_named_map {
            return;
        }
        let ty = inst.write_types.first().map(String::as_str).unwrap_or("");
        if !ty.starts_with('/') {
            return;
        }
        let e = inst.expr.take().unwrap();
        let mut args = vec![ast::str_lit(ty)];
        args.extend(e.args);
        inst.expr = Some(ast::call(
            &format!("struct{}new", keytree::MEMBER_SEP),
            args,
        ));
    }

    fn check_sparse_usage(&mut self, e: &Expr, top_legal: bool) {
        let top_is_sparse = e.op == "map·new";
        let bad = if top_is_sparse && top_legal {
            e.args.iter().any(expr_contains_sparse)
        } else {
            expr_contains_sparse(e)
        };
        if bad {
            let t = self.peek();
            self.errors.push(Diagnostic {
                pos: t.pos,
                warn: false,
                info: false,
                message: "scattered-key array literal {...} is only allowed as an assignment right-hand side (x = {...}) or a for-in source".to_string(),
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
    }

    fn parse_while(&mut self) -> Stmt {
        self.advance(); // consume 'while'
        let cond = self.parse_cond_inst();
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        self.flow.loop_depth += 1;
        let body = self.parse_body();
        self.flow.loop_depth -= 1;
        self.expect(Kind::RBrace);
        Stmt::While(ast::WhileStmt {
            comments: Vec::new(),
            cond,
            body,
        })
    }

    fn parse_block_label(&mut self) -> Stmt {
        let t = self.advance();
        let label = t.value;
        self.flow.labels.push((label.clone(), t.pos));
        self.advance(); // consume Colon
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let body = self.parse_body();
        self.expect(Kind::RBrace);
        Stmt::Scope(ast::ScopeStmt {
            comments: Vec::new(),
            label,
            body,
        })
    }

    fn parse_cond_inst(&mut self) -> Option<Instruction> {
        self.expect(Kind::LParen);
        let mut inst = Instruction::default();
        inst.expr = self.parse_pratt(0);
        self.expect(Kind::RParen);
        Some(inst)
    }

    // ── 指令级（Pratt） ────────────────────────────────────────────

    /// 解析一条「语句级指令」，返回**一或 N 条** Instruction：读槽侧写顶层逗号列表时，
    /// 按位置配对 `writes`、展开为 N 条独立单赋值（见 spec「多赋值」）。个数不等 → error。
    fn parse_inst(&mut self) -> Vec<Instruction> {
        let start_pos = self.peek().pos;
        let mut inst = Instruction::default();
        let mut multi: Vec<Instruction> = Vec::new();

        match self.find_top_level_arrow() {
            Some(v) if v == "=" => {
                inst.arrow_left = true;
                let (writes, wtypes) = self.collect_writes_until_arrow();
                self.advance(); // consume =
                let reads = self.parse_read_list();
                if reads.len() > 1 {
                    multi = self.expand_multi(reads, &writes, &wtypes, true);
                } else {
                    inst.writes = writes;
                    inst.write_types = wtypes;
                    inst.expr = reads.into_iter().next().flatten();
                    self.dispatch_struct_literal(&mut inst);
                    self.lower_array_fill(&mut inst);
                    self.desugar_subscript_write(&mut inst);
                    self.desugar_member_write(&mut inst);
                }
            }
            Some(_) => {
                let reads = self.parse_read_list();
                self.advance(); // consume ->
                let (writes, wtypes) = self.collect_write_list();
                if reads.len() > 1 {
                    multi = self.expand_multi(reads, &writes, &wtypes, false);
                } else {
                    inst.expr = reads.into_iter().next().flatten();
                    inst.writes = writes;
                    inst.write_types = wtypes;
                    self.dispatch_struct_literal(&mut inst);
                    self.desugar_subscript_write(&mut inst);
                    self.desugar_member_write(&mut inst);
                }
            }
            None => {
                if self.peek().kind == Kind::Ident && self.peek_at(1).kind == Kind::Colon {
                    let (name, typ) = self.parse_write_slot();
                    inst.writes = vec![name];
                    inst.write_types = vec![typ];
                    inst.expr = Some(ast::call("compactarray·new", Vec::new()));
                } else {
                    inst.expr = self.parse_pratt(0);
                }
            }
        }

        if self.peek().kind == Kind::Comment {
            self.advance();
        }
        self.eat(Kind::Newline);

        let mut out = if multi.is_empty() { vec![inst] } else { multi };
        // 逐条检查：先 mem::take 移出以避开 &mut self 与 &inst 的借用冲突。
        for k in 0..out.len() {
            let i = std::mem::take(&mut out[k]);
            self.check_write_type_match(&i);
            // 散 key 字面量 `{...}` 仅允许作赋值右值（单一写目标）；其余位置报错。
            let top_legal = i.writes.len() == 1;
            if let Some(e) = &i.expr {
                self.check_sparse_usage(e, top_legal);
                if e.op == "goto" || e.op == "br" {
                    self.flow.jumps.push((e.clone(), start_pos));
                }
            }
            out[k] = i;
        }
        out.retain(|i| i.expr.is_some() || !i.writes.is_empty());
        out
    }

    /// 读槽侧：顶层逗号分隔的表达式列表（多赋值）。单表达式即普通赋值。
    fn parse_read_list(&mut self) -> Vec<Option<Expr>> {
        let mut reads = Vec::new();
        loop {
            reads.push(self.parse_pratt(0));
            if self.peek().kind == Kind::Comma {
                self.advance();
            } else {
                break;
            }
        }
        reads
    }

    /// 多赋值展开：reads 与 writes 按位置配对，各成一条单赋值指令；个数不等 → error。
    fn expand_multi(
        &mut self,
        reads: Vec<Option<Expr>>,
        writes: &[String],
        wtypes: &[String],
        arrow_left: bool,
    ) -> Vec<Instruction> {
        if reads.len() != writes.len() {
            let pos = self.peek().pos;
            self.errors.push(Diagnostic {
                pos,
                warn: false,
                info: false,
                message: format!(
                    "multi-assignment arity mismatch: {} reads vs {} writes",
                    reads.len(),
                    writes.len()
                ),
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
            return Vec::new();
        }
        let mut out = Vec::with_capacity(reads.len());
        for (r, (w, wt)) in reads
            .into_iter()
            .zip(writes.iter().cloned().zip(wtypes.iter().cloned()))
        {
            let mut i = Instruction {
                comments: Vec::new(),
                expr: r,
                writes: vec![w],
                write_types: vec![wt],
                arrow_left,
                operator: false,
                operand_kind: String::new(),
            };
            self.dispatch_struct_literal(&mut i);
            self.lower_array_fill(&mut i);
            self.desugar_subscript_write(&mut i);
            self.desugar_member_write(&mut i);
            out.push(i);
        }
        out
    }

    fn find_top_level_arrow(&self) -> Option<String> {
        let mut depth = 0i32;
        for tok in &self.tokens[self.pos..] {
            match tok.kind {
                Kind::LParen | Kind::LBrace => depth += 1,
                Kind::RParen => depth -= 1,
                Kind::RBrace => {
                    if depth == 0 {
                        return None;
                    }
                    depth -= 1;
                }
                Kind::Arrow => {
                    if depth == 0 {
                        return Some(tok.value.clone());
                    }
                }
                Kind::Newline | Kind::Comment => {
                    if depth == 0 {
                        return None;
                    }
                }
                Kind::EOF => return None,
                _ => {}
            }
        }
        None
    }

    fn parse_pratt(&mut self, min_prec: i32) -> Option<Expr> {
        let mut left = self.parse_primary_expr()?;
        // 成员链：base + 各段收集成单个「= 读」的成员路径表达式（`base·k`/`base·*key`），
        // runtime 直接拼路径读值。不再经 `kvspace·get`（清理方案：get 死刑）。
        // 不在 layout 摊成嵌套成员读（内层返回的是值，丢路径，#110）。每段为静态字面量或
        // 动态键（*k 的变量）。= 读语义：`base·seg… -> v` 即成员路径拷贝读。
        let mut chain_base: Option<Expr> = None;
        let mut chain_segs: Vec<Expr> = Vec::new();
        loop {
            // 后缀成员访问
            if self.peek().kind == Kind::Dot {
                // `·` 作用在指针基上是隐式解引用——必须写 `(*p)·x`（见 spec 05-runtime语义/
                // 04-函数调用「禁止隐式解引用」）。判据只看基座是不是指针形参的裸名。
                if chain_base.is_none() && chain_segs.is_empty() {
                    if let Some(name) = bare_ptr_param(self, &left) {
                        // `·[i]` 是坐标访问（元组键/散 key 下标），报另一条文本。
                        let coordinate = matches!(
                            self.peek_at(1).kind,
                            Kind::LBrack
                        );
                        let pos = self.peek().pos;
                        let message = if coordinate {
                            format!("对指针类型的坐标访问必须显式解引用：写 (*{name})·[i]")
                        } else {
                            format!("对指针类型的成员访问必须显式解引用：写 (*{name})·name")
                        };
                        self.errors.push(Diagnostic {
                            pos,
                            warn: false,
                            info: false,
                            message,
                            source: String::new(),
                            src_file: String::new(),
                            src_name: String::new(),
                        });
                    }
                }
                self.advance(); // consume ·
                                // 动态键 d·*k：段是变量（运行时取值的字符串键）。
                if self.peek().kind == Kind::Ident && self.peek().value == "*" {
                    let star = self.advance(); // consume *
                    if self.peek().kind == Kind::Ident {
                        let key = self.advance().value;
                        if chain_base.is_none() {
                            chain_base = Some(left.clone());
                        }
                        chain_segs.push(ast::leaf(&key));
                        continue;
                    }
                    // `·*<literal>` falls through as a static key (`≡ ·<literal>`); reject complex `·*(...)` keys.
                    if self.peek().kind != Kind::Literal {
                        self.error_at(
                            star.pos,
                            "dynamic member read: expected a name after `·*` (`base·*k`)".to_string(),
                        );
                        return None;
                    }
                }
                // 静态成员：段是字符串字面量。
                if self.peek().kind == Kind::Ident || self.peek().kind == Kind::Literal {
                    let field = self.advance().value;
                    if chain_base.is_none() {
                        chain_base = Some(left.clone());
                    }
                    chain_segs.push(ast::str_lit(&field));
                    continue;
                }
                // 元组/坐标 key：`m·[i,j]` 的成员名**取源码原样文本**（`[39.90,116.40]` 就是
                // `[39.90,116.40]`，不重建、不规范化——key 是独立 langtype 的字面表示，
                // 改写会让写入与读取的 key 对不上）。逐 token 取原文拼接。
                if self.peek().kind == Kind::LBrack {
                    self.advance();
                    let mut parts: Vec<String> = vec!["[".to_string()];
                    while self.peek().kind != Kind::RBrack && self.peek().kind != Kind::EOF {
                        let t = self.advance();
                        if t.kind == Kind::Comma {
                            parts.push(",".to_string());
                        } else if t.kind != Kind::Newline && t.kind != Kind::Comment {
                            parts.push(t.value.clone());
                        }
                    }
                    self.expect(Kind::RBrack);
                    parts.push("]".to_string());
                    if chain_base.is_none() {
                        chain_base = Some(left.clone());
                    }
                    chain_segs.push(ast::str_lit(&parts.concat()));
                    continue;
                }
            }
            // 成员链被打断（下标/中缀/循环尾）：flush 成单个成员路径读 `base·seg…`。
            // `base·seg…` 的读语义 = 成员路径拷贝读（runtime 拼路径读值），不再经 kvspace·get。
            if let Some(base) = chain_base.take() {
                let mut args = vec![base];
                args.append(&mut chain_segs);
                left = ast::call(MEMBER_READ_OP, args);
            }
            // 后缀索引
            if self.peek().kind == Kind::LBrack {
                // `[` 作用在指针基上同样是隐式解引用——写 `(*p)[i]`。
                if let Some(name) = bare_ptr_param(self, &left) {
                    let pos = self.peek().pos;
                    self.errors.push(Diagnostic {
                        pos,
                        warn: false,
                        info: false,
                        message: format!(
                            "对指针类型的坐标访问必须显式解引用：写 (*{name})[i]"
                        ),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                self.advance();
                let mut indices = Vec::new();
                while self.peek().kind != Kind::RBrack && self.peek().kind != Kind::EOF {
                    if self.eat(Kind::Comma) {
                        continue;
                    }
                    self.parse_list_item(&mut indices, "subscript");
                }
                self.expect(Kind::RBrack);
                let is_path = left.is_leaf() && left.val.starts_with('/');
                let mut args = vec![left];
                args.extend(indices);
                // 路径字面量 + [idx] → 成员路径读（`=` 读，KV 路径下标访问）；否则 xv.at（compact 数组元素）。
                left = ast::call(if is_path { MEMBER_READ_OP } else { "compactarray·at" }, args);
                continue;
            }
            let t = self.peek();
            if t.kind != Kind::Ident {
                break;
            }
            let e = symbol::lookup(&t.value);
            let prec = e.precedence;
            if prec == 0 {
                // 一元算子（`*`/`!`/`√`，precedence 0）出现在操作数之后 = 中缀位置：
                // 静默错（`a * b` 只取 a、`a * b + 2` 运行时错），违反 p1，编译期硬错。
                if e.unary && !e.word.is_empty() {
                    let hint = if e.word == "pointer" {
                        "（乘号用 ×）"
                    } else {
                        "（该一元算子不支持中缀）"
                    };
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: false,
                        message: format!("一元算子 {:?} 出现在操作数之后{hint}", t.value),
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                    return None;
                }
                break;
            }
            if prec <= min_prec {
                break;
            }
            let op = self.advance().value;
            let right = self.parse_pratt(prec)?;
            left = ast::call(&op, vec![left, right]);
        }
        if let Some(base) = chain_base.take() {
            let mut args = vec![base];
            args.append(&mut chain_segs);
            left = ast::call(MEMBER_READ_OP, args);
        }
        Some(left)
    }

    fn parse_primary_expr(&mut self) -> Option<Expr> {
        let t = self.peek();
        match t.kind {
            Kind::Arrow
            | Kind::RParen
            | Kind::RBrack
            | Kind::Newline
            | Kind::RBrace
            | Kind::EOF
            | Kind::Comma
            | Kind::Comment => return None,
            _ => {}
        }

        // 一元前缀算子
        if t.kind == Kind::Ident && is_unary_prefix_op(&t.value) {
            self.advance();
            if symbol::lookup(&t.value).word == "sub" {
                let next = self.peek();
                if next.kind == Kind::Literal
                    && !next.value.is_empty()
                    && next.value.as_bytes()[0].is_ascii_digit()
                {
                    let lit = self.advance();
                    let neg = format!("-{}", lit.value);
                    if is_float_literal(&neg) {
                        return Some(ast::float_lit(&neg));
                    }
                    return Some(ast::int_lit(&neg));
                }
            }
            if symbol::lookup(&t.value).word == "add" {
                return self.parse_pratt(UNARY_PREC);
            }
            // 一元前缀 * = 解引用：*p 读/写该 Ptr 槽的目标（与形参槽 *[0,±k] 同一读参命名约定，
            // 见 spec [[ptr]]）。操作数先按 UNARY_PREC 结合（*p·x ≡ *(p·x)，同 C），非叶操作数由
            // lower 展开成临时槽后再取 *名——runtime 一律按「读该槽 → 槽里是 Ptr → 落其目标」。
            if symbol::lookup(&t.value).word == "pointer" {
                let arg = self.parse_pratt(UNARY_PREC)?;
                self.check_deref_operand(&arg, t.pos);
                return Some(ast::call(ast::DEREF_OP, vec![arg]));
            }
            // 一元前缀 & = 取址：&x ≡ kvlang·abs(x)（中缀 & 仍为按位与，走 pratt 中缀路径）。
            // & 对成员链（`=` 读）→ kvlang·abs(base, segs...)：取成员路径地址，非读值取址。
            if symbol::lookup(&t.value).word == "bitand" {
                let arg = self.parse_pratt(UNARY_PREC)?;
                let op = format!("kvlang{}abs", keytree::MEMBER_SEP);
                if arg.op == ast::MEMBER_READ_OP && arg.args.len() >= 2 {
                    return Some(ast::call(&op, arg.args));
                }
                return Some(ast::call(&op, vec![arg]));
            }
            let arg = self.parse_pratt(UNARY_PREC)?;
            return Some(ast::call(&t.value, vec![arg]));
        }

        // 数组字面量
        // 数组字面量 `[...]` → compact 数组：元素连续打包进单个 XValue（定长）。
        // 与散 key 数组字面量 `{...}` 相对——compact 要求元素定长，不接受变长字符串；
        // 想要字符串数组必须写成散 key `{"a","b"}`。
        if t.kind == Kind::LBrack {
            self.advance();
            let mut elems = Vec::new();
            let mut fill = false;
            while self.peek().kind != Kind::RBrack && self.peek().kind != Kind::EOF {
                if self.eat(Kind::Comma) {
                    continue;
                }
                // 尾缀 `...`（词法为单个 Ident "..."）：填充标记（`[v...]` 全填 v）。
                if self.peek().kind == Kind::Ident && self.peek().value == "..." {
                    self.advance();
                    fill = true;
                    break;
                }
                self.parse_list_item(&mut elems, "array literal");
            }
            self.expect(Kind::RBrack);
            if fill {
                if elems.len() != 1 {
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: false,
                        info: false,
                        message: "fill array [v...] requires exactly one fill value".to_string(),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                // 填充值先挂 array·fill；langtype（维度来源）在指令级由写类型补齐。
                return Some(ast::call("compactarray·fill", elems));
            }
            if let Some(bad) = elems.iter().find(|e| e.is_leaf() && e.quote != 0) {
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: false,
                    info: false,
                    message: format!(
                        "compact array [...] cannot hold variable-length string element {:?}; use a scattered-key array {{...}} for string arrays",
                        bad.val
                    ),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
            return Some(ast::call("compactarray·new", elems));
        }

        // struct 字面量 Name{...} 或 pkg.Name{...} → struct·new("/lib/…", "f", v, …)
        let struct_lit = t.kind == Kind::Ident
            && (self.peek_at(1).kind == Kind::LBrace || {
                let mut j = 1isize;
                while self.peek_at(j).kind == Kind::Dot && self.peek_at(j + 1).kind == Kind::Ident {
                    j += 2;
                }
                j > 1 && self.peek_at(j).kind == Kind::LBrace
            });
        if struct_lit {
            let mut path = format!("{}/{}", keytree::LIB_ROOT, self.advance().value);
            while self.peek().kind == Kind::Dot && self.peek_at(1).kind == Kind::Ident {
                self.advance(); // skip Dot
                path.push_str(keytree::MEMBER_SEP);
                path.push_str(&self.advance().value);
            }
            let mut args = vec![ast::str_lit(&path)];
            self.advance(); // consume {
            loop {
                while matches!(
                    self.peek().kind,
                    Kind::Newline | Kind::Comma | Kind::Comment
                ) {
                    self.advance();
                }
                if self.peek().kind == Kind::RBrace || self.peek().kind == Kind::EOF {
                    break;
                }
                if self.peek().kind != Kind::Ident {
                    let bt = self.peek();
                    self.errors.push(Diagnostic {
                        pos: bt.pos,
                        warn: false,
                        info: false,
                        message: format!("struct literal: expected field name, got {:?}", bt.value),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                    break;
                }
                let key = self.advance().value;
                if !(self.peek().kind == Kind::Arrow && self.peek().value == "=") {
                    let bt = self.peek();
                    self.errors.push(Diagnostic {
                        pos: bt.pos,
                        warn: false,
                        info: false,
                        message: format!("struct literal: expected '=' after {key:?}"),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                    break;
                }
                self.advance(); // consume =
                let val = match self.parse_pratt(0) {
                    Some(v) => v,
                    None => break,
                };
                args.push(ast::str_lit(&key));
                args.push(val);
            }
            self.expect(Kind::RBrace);
            return Some(ast::call(
                &format!("struct{}new", keytree::MEMBER_SEP),
                args,
            ));
        }

        // 容器字面量 `{…}`。位置型与具名型是**两种不同的东西**，用不同算子承载：
        //
        // - 位置型 `{v0, v1, …}` → `map·new(v0, v1, …)`：map 容器，成员键由键侧
        //   formatter 产出；
        // - 具名型 `{k=v, …}` → 待定的 struct 实例化：成员名来自原型，最终是否成立取决于
        //   写槽 langtype（是 structref 才合法）。用独立算子承载，让分派处能分清，
        //   否则「位置型字符串列表」与「具名字段表」在 args 里长得一样。
        if t.kind == Kind::LBrace {
            let mut j = 1isize;
            while self.peek_at(j).kind == Kind::Newline || self.peek_at(j).kind == Kind::Comment {
                j += 1;
            }
            let named = self.peek_at(j).kind == Kind::Ident
                && self.peek_at(j + 1).kind == Kind::Arrow
                && self.peek_at(j + 1).value == "=";
            self.advance(); // consume {
            let mut elems = Vec::new();
            loop {
                while matches!(
                    self.peek().kind,
                    Kind::Newline | Kind::Comma | Kind::Comment
                ) {
                    self.advance();
                }
                if self.peek().kind == Kind::RBrace || self.peek().kind == Kind::EOF {
                    break;
                }
                if named {
                    // `{k=v …}`：键是标识符，值接在后面，键值交错。
                    let key = self.advance().value;
                    self.advance(); // consume =
                    let Some(val) = self.parse_pratt(0) else { break };
                    elems.push(ast::str_lit(&key));
                    elems.push(val);
                } else {
                    match self.parse_pratt(0) {
                        Some(e) => elems.push(e),
                        None => break,
                    }
                }
            }
            self.expect(Kind::RBrace);
            // 具名成员 `{n=42; x=7}` 与位置成员 `{10, 20}` 都是「键值组」（groupkv）的
            // 构造，不存在第二种构造算子——一律 `map·new`。字面量**替换**已有成员，
            // 这一点由 `map·new` 的既有行为决定，写法不因键形态而分叉。
            return Some(ast::call("map·new", elems));
        }

        // 括号分组
        if t.kind == Kind::LParen {
            self.advance();
            let expr = self.parse_pratt(0);
            self.expect(Kind::RParen);
            return expr;
        }

        // 函数调用 name(...)
        if self.peek_at(1).kind == Kind::LParen {
            let name = self.advance().value;
            if name == "return" {
                let pos = self
                    .tokens
                    .get(self.pos.saturating_sub(1))
                    .map(|t| t.pos)
                    .unwrap_or(Pos { line: 0, col: 0 });
                self.errors.push(Diagnostic {
                    pos,
                    message: "return 不接受参数；直接写 return 即可，返回值通过写参零拷贝传递"
                        .to_string(),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
            self.advance(); // consume (
            let mut args = Vec::new();
            while self.peek().kind != Kind::RParen && self.peek().kind != Kind::EOF {
                if self.eat(Kind::Comma) {
                    continue;
                }
                self.parse_list_item(&mut args, "argument list");
            }
            self.expect(Kind::RParen);
            return Some(ast::call(&name, args));
        }

        // 点号函数调用 name.name(...) 或 /lib/name.name(...)
        let is_dotted_call = {
            let lhs = self.peek();
            let lhs_ok = lhs.kind == Kind::Ident
                || (lhs.kind == Kind::Literal
                    && !lhs.value.is_empty()
                    && lhs.value.as_bytes()[0] == b'/');
            // `*` after `·` starts a dynamic member key (`·*k`), never a call opcode `m·*`.
            let seg = |t: Token| t.kind == Kind::Ident && t.value != ast::DEREF_OP;
            lhs_ok && self.peek_at(1).kind == Kind::Dot && seg(self.peek_at(2)) && {
                let mut j = 3isize;
                while self.peek_at(j).kind == Kind::Dot && seg(self.peek_at(j + 1)) {
                    j += 2;
                }
                self.peek_at(j).kind == Kind::LParen
            }
        };
        if is_dotted_call {
            let mut opcode = self.advance().value;
            while self.peek().kind == Kind::Dot && self.peek_at(1).kind == Kind::Ident {
                self.advance(); // skip Dot
                opcode.push_str(keytree::MEMBER_SEP);
                opcode.push_str(&self.advance().value);
            }
            self.advance(); // consume (
            let mut args = Vec::new();
            while self.peek().kind != Kind::RParen && self.peek().kind != Kind::EOF {
                if self.eat(Kind::Comma) {
                    continue;
                }
                self.parse_list_item(&mut args, "argument list");
            }
            self.expect(Kind::RParen);
            return Some(ast::call(&opcode, args));
        }

        // 斜杠函数调用 char/utf8(...)
        if self.peek().kind == Kind::Ident
            && self.peek_at(1).kind == Kind::Literal
            && !self.peek_at(1).value.is_empty()
            && self.peek_at(1).value.as_bytes()[0] == b'/'
            && self.peek_at(2).kind == Kind::LParen
        {
            let mut opcode = self.advance().value;
            opcode.push_str(&self.advance().value);
            self.advance(); // consume (
            let mut args = Vec::new();
            while self.peek().kind != Kind::RParen && self.peek().kind != Kind::EOF {
                if self.eat(Kind::Comma) {
                    continue;
                }
                self.parse_list_item(&mut args, "argument list");
            }
            self.expect(Kind::RParen);
            return Some(ast::call(&opcode, args));
        }

        // 叶节点
        let t = self.advance();
        if t.kind == Kind::Literal {
            let v = t.value;
            if t.quote == b'"' {
                return Some(ast::str_lit(&v));
            }
            if t.quote == b'r' {
                return Some(ast::raw_str(&v));
            }
            if !v.is_empty() && v.as_bytes()[0] == b'/' {
                return Some(ast::leaf(&v));
            }
            if !v.is_empty() && v.as_bytes()[0].is_ascii_digit() {
                if !is_numeric_literal(&v) {
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        message: format!("invalid numeric literal {v:?}"),
                        warn: false,
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                    return Some(ast::leaf(&v));
                }
                if is_float_literal(&v) {
                    return Some(ast::float_lit(&v));
                }
                return Some(ast::int_lit(&v));
            }
            return Some(ast::str_lit(&v));
        }
        if t.kind == Kind::Return {
            let next = self.peek();
            match next.kind {
                Kind::Newline
                | Kind::EOF
                | Kind::RBrace
                | Kind::RParen
                | Kind::RBrack
                | Kind::Comma
                | Kind::Arrow
                | Kind::Comment => {}
                _ => {
                    self.errors.push(Diagnostic {
                        pos: next.pos,
                        message: "return cannot take a value — use write-params for output"
                            .to_string(),
                        warn: false,
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
            }
        }
        if t.kind == Kind::Ident && t.value == "null" {
            self.errors.push(Diagnostic {
                pos: t.pos,
                message: "null 不是合法字面量；空值只有 None".to_string(),
                warn: false,
                info: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        Some(ast::leaf(&t.value))
    }

    // ── 写槽收集 ─────────────────────────────────────────────────

    fn collect_write_list(&mut self) -> (Vec<String>, Vec<String>) {
        // `(` 有两种含义：写参列表 `-> (a, b)` / `-> (*a, _)`，或解引用基座 `-> (*p)·k`。
        // 判据不能只看括号内是不是 `*`——`(*a, _)` 也是写参列表。**必须看 `)` 之后是不是 `·`**：
        // 只有 `(*name)·` 才是解引用后的成员写槽。
        let is_deref_member = self.peek().kind == Kind::LParen
            && self.peek_at(1).kind == Kind::Ident
            && self.peek_at(1).value == ast::DEREF_OP
            && self.peek_at(2).kind == Kind::Ident
            && self.peek_at(3).kind == Kind::RParen
            && self.peek_at(4).kind == Kind::Dot;
        let is_write_param_list = self.peek().kind == Kind::LParen && !is_deref_member;
        if is_write_param_list {
            self.advance();
            let mut writes = Vec::new();
            let mut wtypes = Vec::new();
            while self.peek().kind != Kind::RParen && self.peek().kind != Kind::EOF {
                if self.eat(Kind::Comma) {
                    continue;
                }
                let (name, typ) = self.parse_write_slot();
                writes.push(name);
                wtypes.push(typ);
            }
            self.expect(Kind::RParen);
            return (writes, wtypes);
        }
        let mut writes = Vec::new();
        let mut wtypes = Vec::new();
        loop {
            let t = self.peek();
            if matches!(
                t.kind,
                Kind::Newline | Kind::RBrace | Kind::EOF | Kind::RParen | Kind::Comment
            ) {
                break;
            }
            if t.kind == Kind::Comma {
                self.advance();
                continue;
            }
            // 路径字面量 = 未加引号（或单引号裸字面量，见 01-词法/07）。
            // 带引号（"…"/r"…"）一律字符串，即使内容以 `/` 开头。
            let is_path_literal = t.kind == Kind::Literal
                && t.quote == 0
                && !t.value.is_empty()
                && t.value.as_bytes()[0] == b'/';
            let is_quoted_literal = t.kind == Kind::Literal && t.quote != 0;
            let is_ident = t.kind == Kind::Ident;
            let is_call_start = is_ident && self.peek_at(1).kind == Kind::LParen;
            let is_invalid_literal = t.kind == Kind::Literal && !is_path_literal;

            if is_call_start {
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: true,
                    message: format!("function call {:?} on same line as write slot — each instruction must be on its own line", t.value),
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                return (writes, wtypes);
            }
            // 下标写槽 arr[i,j] / arr·[i,j]：整段含括号收成单个 write 槽，交给 desugar_subscript_write。
            if t.kind == Kind::Ident
                && (self.peek_at(1).kind == Kind::LBrack
                    || (self.peek_at(1).kind == Kind::Dot && self.peek_at(2).kind == Kind::LBrack))
            {
                let mut w = self.advance().value; // base
                if self.peek().kind == Kind::Dot {
                    self.advance();
                    w.push_str(keytree::MEMBER_SEP);
                }
                w.push_str(&self.advance().value); // [
                let mut depth = 1i32;
                while depth > 0
                    && !matches!(self.peek().kind, Kind::EOF | Kind::Newline | Kind::RBrace)
                {
                    if self.peek().kind == Kind::RBrack {
                        depth -= 1;
                    }
                    if self.peek().kind == Kind::LBrack {
                        depth += 1;
                    }
                    w.push_str(&self.advance().value);
                }
                if depth > 0 {
                    let pos = self.peek().pos;
                    self.error_at(pos, format!("unterminated `[` in write slot {w:?}"));
                }
                writes.push(w);
                wtypes.push(String::new());
                continue;
            }
            // 成员链写槽优先于「非法字面量」判定：引号 base 后紧跟 `·` 是规范的显式形态
            // （`"/x"·*k`），不带 `·` 的引号串才报「不是路径」。
            let member_chain = (is_ident
                && self.peek_at(1).kind == Kind::Dot
                && self.peek_at(2).kind != Kind::LBrack)
                || (is_path_literal && self.peek_at(1).kind == Kind::Dot)
                || (is_quoted_literal
                    && self.peek_at(1).kind == Kind::Dot
                    && self.peek_at(2).kind != Kind::LBrack);
            if member_chain {
                let base_token = self.peek();
                let mut w = self.advance().value;
                // `·` 作用在指针基上是隐式解引用——**写位置不给例外**，与读同一判据。
                // `*` 是显式解引用，任何位置都不得省略（spec 16-ptr）。
                if self.ptr_params.contains(&w) {
                    self.errors.push(Diagnostic {
                        pos: base_token.pos,
                        warn: false,
                        info: false,
                        message: format!(
                            "对指针类型的成员访问必须显式解引用：写 `(*{w})·name`"
                        ),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                // 成员链写槽：整段 p·a·b·c 收成单个 write 槽，`=` 写（成员路径写）
                // runtime ResolveWriteSlot 拼成员绝对路径。勿在每段 · 处截断
                // （否则 deep 被当独立写槽）。
                while self.peek().kind == Kind::Dot {
                    self.advance(); // .
                    w.push_str(keytree::MEMBER_SEP);
                    if !self.member_write_segment(&mut w) {
                        break;
                    }
                }
                writes.push(w);
                wtypes.push(String::new());
                continue;
            }
            // `(*p)·seg…` / `(*p)[i]`：解引用后做成员写或下标写。
            // `*` 不得省略，基座加括号才不会读成 `*(p·seg)`。
            let is_deref_base = t.kind == Kind::LParen
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(1).value == ast::DEREF_OP
                && self.peek_at(2).kind == Kind::Ident
                && self.peek_at(3).kind == Kind::RParen
                && matches!(self.peek_at(4).kind, Kind::Dot | Kind::LBrack);
            if is_deref_base {
                let mut w = String::from("(*");
                self.advance(); // (
                self.advance(); // *
                w.push_str(&self.advance().value); // 名字
                self.advance(); // )
                w.push(')');
                if self.peek().kind == Kind::LBrack {
                    // `(*p)[i] = v`：整段收成 `(*p)[i]` 写槽，交给 desugar_subscript_write。
                    self.advance(); // [
                    w.push('[');
                    let mut depth = 1;
                    while self.peek().kind != Kind::EOF && depth > 0 {
                        let t = self.peek();
                        if t.kind == Kind::LBrack {
                            depth += 1;
                        } else if t.kind == Kind::RBrack {
                            depth -= 1;
                            if depth == 0 {
                                self.advance();
                                break;
                            }
                        }
                        w.push_str(&t.value);
                        self.advance();
                    }
                    w.push(']');
                } else {
                    while self.peek().kind == Kind::Dot {
                        self.advance(); // .
                        w.push_str(keytree::MEMBER_SEP);
                        if !self.member_write_segment(&mut w) {
                            break;
                        }
                    }
                }
                writes.push(w);
                wtypes.push(String::new());
                continue;
            }
            // 规范 04-layout语义/10-常见诊断：引号串不是路径（成员 base 已被上面的分支消费）。
            if is_quoted_literal && t.value.as_bytes().first() == Some(&b'/') {
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: false,
                    message: format!(
                        "string {:?} is not a path; absolute paths must be unquoted",
                        t.value
                    ),
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                return (writes, wtypes);
            }
            if is_invalid_literal || (!is_ident && !is_path_literal) {
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: true,
                    message: format!("unexpected token {:?} in write slot position — did you put two instructions on the same line? each instruction must be on its own line", t.value),
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                return (writes, wtypes);
            }
            let (name, typ) = self.parse_write_slot();
            writes.push(name);
            wtypes.push(typ);
        }
        (writes, wtypes)
    }

    /// 解引用操作数检查：须是名字（Ptr 槽）；地址形参名不可再 `*`（体内已解引用一次 = 两层间接）。
    fn check_deref_operand(&mut self, arg: &Expr, pos: Pos) {
        if !arg.is_leaf() {
            return;
        }
        let msg = if arg.quote != 0 {
            "`*` 解引用要求 Ptr 槽（名字），不能作用于字符串字面量".to_string()
        } else if self.value_params.contains(&arg.val) {
            // 值复制形参是体内的一份副本，只读、没有可写的目标：
            // 对它解引用等于凭空造一个指针（spec 05-runtime语义/04-函数调用）。
            format!(
                "值复制参数不可解引用：只读，没有可写的目标（`{}` 是值复制形参）",
                arg.val
            )
        } else {
            return;
        };
        self.errors.push(Diagnostic {
            pos,
            message: msg,
            warn: false,
            info: false,
            source: String::new(),
            src_file: String::new(),
            src_name: String::new(),
        });
    }

    /// Parse one write-slot member segment (`·` already consumed); returns whether chaining may continue.
    /// Accepts static names/literals (`base·0`, `base·"c"`), `·*<literal>` (`≡ ·<literal>`), and single-name
    /// dynamic keys `base·*name`; rejects missing names or compound expressions like `·*k+1`.
    fn member_write_segment(&mut self, w: &mut String) -> bool {
        let t = self.peek();
        if t.kind == Kind::Ident && t.value == ast::DEREF_OP {
            let next = self.peek_at(1);
            let is_path = next.kind == Kind::Literal && next.value.starts_with('/');
            match next.kind {
                Kind::Ident => {
                    self.advance(); // *
                    w.push_str(ast::DEREF_OP);
                    w.push_str(&self.advance().value);
                    let after = self.peek();
                    if after.kind == Kind::Ident && symbol::lookup(&after.value).precedence > 0 {
                        self.error_at(
                            after.pos,
                            format!(
                                "dynamic member write key must be a single name (`base·*k`), got `·*{}{}…`; compute the key into a variable first",
                                next.value, after.value
                            ),
                        );
                        self.skip_rest_of_slot();
                        return false;
                    }
                    true
                }
                Kind::Literal if !is_path => {
                    self.advance(); // *
                    w.push_str(&self.advance().value);
                    true
                }
                _ => {
                    self.error_at(
                        t.pos,
                        "dynamic member write: expected a name after `·*` (`base·*k`)".to_string(),
                    );
                    self.advance(); // *
                    self.skip_rest_of_slot();
                    false
                }
            }
        } else if t.kind == Kind::Ident || t.kind == Kind::Literal {
            w.push_str(&self.advance().value);
            true
        } else {
            false
        }
    }

    /// Skip remaining tokens of a malformed write-slot list up to newline, `}`, or assignment arrow.
    fn skip_rest_of_slot(&mut self) {
        while !matches!(
            self.peek().kind,
            Kind::Newline | Kind::RBrace | Kind::EOF | Kind::Arrow
        ) {
            self.advance();
        }
    }

    fn parse_write_slot(&mut self) -> (String, String) {
        // 解引用写槽 `*p`：写穿该 Ptr 槽的目标（与读参 `*p` 同一约定，见 spec [[ptr]]）。
        // 只接**名字**——成员写 `p·x` 自带按指针优先、下标写先 `*p` 解引用到变量，故 `*` 后
        // 跟 `·`/`[` 一律报错，不静默错拼成两个写槽。
        let mut name = String::new();
        // `(*p)·seg…`：解引用后做成员写。`*` 是显式解引用，写位置同样不得省略，
        // 而 `*p·seg` 会被读成 `*(p·seg)`，所以基座必须加括号。
        if self.peek().kind == Kind::LParen
            && self.peek_at(1).kind == Kind::Ident
            && self.peek_at(1).value == ast::DEREF_OP
            && self.peek_at(2).kind == Kind::Ident
            && self.peek_at(3).kind == Kind::RParen
        {
            self.advance(); // (
            self.advance(); // *
            name.push('(');
            name.push_str(ast::DEREF_OP);
            name.push_str(&self.advance().value); // 名字
            self.advance(); // )
            name.push(')');
            if self.peek().kind == Kind::Dot {
                self.advance();
                name.push_str(keytree::MEMBER_SEP);
                self.member_write_segment(&mut name);
            }
            return (name, String::new());
        }
        if self.peek().kind == Kind::Ident
            && self.peek().value == ast::DEREF_OP
            && self.peek_at(1).kind == Kind::Ident
        {
            self.advance();
            name.push_str(ast::DEREF_OP);
        }
        name.push_str(&self.advance().value);
        // 参数槽本身永远不可写：形参槽存的是指针，裸写 `-> p` 改的是**指针绑定**而不是
        // 它指向的目标，等于偷偷换掉调用方的地址。要写目标必须写 `-> *p`。
        if !name.starts_with(ast::DEREF_OP) && self.ptr_params.contains(&name) {
            self.errors.push(Diagnostic {
                pos: self.peek().pos,
                warn: false,
                info: false,
                message: format!("写指针形参必须显式解引用：写 `-> *{name}`"),
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        if name.starts_with(ast::DEREF_OP) && matches!(self.peek().kind, Kind::Dot | Kind::LBrack) {
            self.errors.push(Diagnostic {
                pos: self.peek().pos,
                message:
                    "`*` 解引用写槽只接名字（如 `*p`）：成员写用 `p·x`，下标写先 `*p` 解引用到变量"
                        .to_string(),
                warn: false,
                info: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        let mut typ = String::new();
        if self.peek().kind == Kind::Colon {
            self.advance();
            typ = self.parse_type();
            if typ.is_empty() {
                self.errors.push(Diagnostic {
                    pos: self.peek().pos,
                    message: "empty kindexp is only allowed in signature slots (`name:`);                               变量/字面量标注必须写具体类型"
                        .to_string(),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
            if typ == "int" || typ == "float" {
                let pos = self.peek().pos;
                self.errors.push(Diagnostic {
                    pos,
                    message: format!(
                        "ambiguous type {typ:?} in write slot — use int64 or float64 instead"
                    ),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        (name, typ)
    }

    fn collect_writes_until_arrow(&mut self) -> (Vec<String>, Vec<String>) {
        // `(` 有两种含义：`= (a, b)` 写参列表，或解引用基座 `= (*p)·k` / `= (*p)[i]`。
        // 判据看 `)` 之后是不是 `·` 或 `[`——只有那种才是解引用后的成员/下标写槽。
        let is_deref_base = self.peek().kind == Kind::LParen
            && self.peek_at(1).kind == Kind::Ident
            && self.peek_at(1).value == ast::DEREF_OP
            && self.peek_at(2).kind == Kind::Ident
            && self.peek_at(3).kind == Kind::RParen
            && matches!(self.peek_at(4).kind, Kind::Dot | Kind::LBrack);
        let has_paren = self.peek().kind == Kind::LParen && !is_deref_base;
        if has_paren {
            self.advance();
        }
        let mut writes = Vec::new();
        let mut wtypes = Vec::new();
        loop {
            let t = self.peek();
            if t.kind == Kind::Comment || (!has_paren && t.kind == Kind::Newline) {
                self.advance();
                continue;
            }
            if t.kind == Kind::Arrow || t.kind == Kind::EOF {
                break;
            }
            if has_paren && t.kind == Kind::RParen {
                self.advance();
                break;
            }
            if t.kind == Kind::Comma {
                self.advance();
                continue;
            }
            // `(*p)·k` / `(*p)[i]`：解引用基座的成员/下标写槽，整段收成一个写槽。
            // 这条必须在普通 ident 收集之前——否则 `(` 会被当成一个独立写槽，
            // 括号内每个 token 各成一个槽（`(*r)[*i]` 被拆成 4 个）。
            let is_deref_base_here = t.kind == Kind::LParen
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(1).value == ast::DEREF_OP
                && self.peek_at(2).kind == Kind::Ident
                && self.peek_at(3).kind == Kind::RParen
                && matches!(self.peek_at(4).kind, Kind::Dot | Kind::LBrack);
            if is_deref_base_here {
                let mut w = String::from("(*");
                self.advance(); // (
                self.advance(); // *
                w.push_str(&self.advance().value); // 名字
                self.advance(); // )
                w.push(')');
                if self.peek().kind == Kind::LBrack {
                    self.advance(); // [
                    w.push('[');
                    let mut depth = 1;
                    while self.peek().kind != Kind::EOF && depth > 0 {
                        let inner = self.peek();
                        if inner.kind == Kind::LBrack {
                            depth += 1;
                        } else if inner.kind == Kind::RBrack {
                            depth -= 1;
                            if depth == 0 {
                                self.advance();
                                break;
                            }
                        }
                        w.push_str(&inner.value);
                        self.advance();
                    }
                    w.push(']');
                } else {
                    while self.peek().kind == Kind::Dot {
                        self.advance();
                        w.push_str(keytree::MEMBER_SEP);
                        if !self.member_write_segment(&mut w) {
                            break;
                        }
                    }
                }
                writes.push(w);
                wtypes.push(String::new());
                continue;
            }
            let is_path_lit = t.kind == Kind::Literal
                && t.quote == 0
                && !t.value.is_empty()
                && t.value.as_bytes()[0] == b'/';
            let is_quoted_lit = t.kind == Kind::Literal && t.quote != 0;
            // 成员链写槽优先：引号 base 后紧跟 `·` 是规范的显式形态（`"/x"·*k`）。
            let is_string_member_base = is_quoted_lit
                && self.peek_at(1).kind == Kind::Dot
                && self.peek_at(2).kind != Kind::LBrack;
            if (t.kind == Kind::Ident || is_path_lit || is_string_member_base)
                && self.peek_at(1).kind == Kind::Dot
                && self.peek_at(2).kind != Kind::LBrack
            {
                let mut w = self.advance().value;
                // `·` 作用在指针基上是隐式解引用——写位置与读同一判据。
                if self.ptr_params.contains(&w) {
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: false,
                        info: false,
                        message: format!("对指针类型的成员访问必须显式解引用：写 `(*{w})·name`"),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                // 成员链写槽：整段 p·a·b·c 收成单个 write 槽（勿在每段 · 处截断）。
                while self.peek().kind == Kind::Dot && self.peek_at(1).kind != Kind::LBrack {
                    self.advance(); // .
                    w.push_str(keytree::MEMBER_SEP);
                    if !self.member_write_segment(&mut w) {
                        break;
                    }
                }
                // 成员写槽可带 langtype 注解 `a·1:Node = {…}`：struct 对象定义的推荐形态。
                let mut ty = String::new();
                if self.peek().kind == Kind::Colon {
                    self.advance();
                    ty = self.parse_type();
                    if ty.is_empty() {
                        self.errors.push(Diagnostic {
                            pos: Pos { line: 0, col: 0 },
                            message: "empty kindexp is only allowed in signature slots                                       (`name:`); 成员写槽标注必须写具体类型"
                                .to_string(),
                            warn: false,
                            info: false,
                            source: String::new(),
                            src_file: String::new(),
                            src_name: String::new(),
                        });
                    }
                }
                writes.push(w);
                wtypes.push(ty);
                continue;
            }
            // 规范 04-layout语义/10-常见诊断：引号串不是路径（成员 base 已被上面的分支消费）。
            if is_quoted_lit && t.value.as_bytes().first() == Some(&b'/') {
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: false,
                    message: format!(
                        "string {:?} is not a path; absolute paths must be unquoted",
                        t.value
                    ),
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                self.advance();
                continue;
            }
            if t.kind == Kind::Ident && self.peek_at(1).kind == Kind::LBrack {
                let mut w = self.advance().value;
                w.push_str(&self.advance().value); // [
                let mut depth = 1i32;
                while depth > 0 && self.peek().kind != Kind::EOF && self.peek().kind != Kind::Arrow
                {
                    if self.peek().kind == Kind::RBrack {
                        depth -= 1;
                    }
                    if self.peek().kind == Kind::LBrack {
                        depth += 1;
                    }
                    w.push_str(&self.advance().value);
                }
                if depth > 0 {
                    let pos = self.peek().pos;
                    self.error_at(pos, format!("unterminated `[` in write slot {w:?}"));
                }
                writes.push(w);
                wtypes.push(String::new());
                continue;
            }
            if t.kind == Kind::Ident
                && self.peek_at(1).kind == Kind::Dot
                && self.peek_at(2).kind == Kind::LBrack
            {
                let mut w = self.advance().value; // base
                self.advance(); // .
                w.push_str(keytree::MEMBER_SEP);
                w.push_str(&self.advance().value); // [
                let mut depth = 1i32;
                while depth > 0 && self.peek().kind != Kind::EOF && self.peek().kind != Kind::Arrow
                {
                    if self.peek().kind == Kind::RBrack {
                        depth -= 1;
                    }
                    if self.peek().kind == Kind::LBrack {
                        depth += 1;
                    }
                    w.push_str(&self.advance().value);
                }
                if depth > 0 {
                    let pos = self.peek().pos;
                    self.error_at(pos, format!("unterminated `[` in write slot {w:?}"));
                }
                writes.push(w);
                wtypes.push(String::new());
                continue;
            }
            let (name, typ) = self.parse_write_slot();
            writes.push(name);
            wtypes.push(typ);
        }
        (writes, wtypes)
    }

    // 下标写脱糖：arr[i,j] 写槽 + 值 e → xv·set(arr, i, j, e) -> arr（compact 数组，
    // 读侧 arr[i,j]→xv·at 的对称）。arr· 前缀坐标或 / 路径 → 成员写槽
    // `arr·[i,j]`（`=` 写，runtime 拼成员路径）。左右箭头共用：= 时 e 是 pratt 右值，
    // -> 时 e 是箭头左值，语义一致。layout 不判维数，交给 runtime。
    fn desugar_subscript_write(&mut self, inst: &mut Instruction) {
        if inst.writes.len() != 1 || !inst.writes[0].contains('[') {
            return;
        }
        let s = inst.writes[0].clone();
        let br = s.find('[').unwrap_or(s.len());
        let arr = s[..br].to_string();
        // Strip trailing `]` safely even when `[` is unclosed or the final byte is non-ASCII.
        let end = s.len().saturating_sub(1).max(br + 1);
        let idxs = s.get(br + 1..end).unwrap_or("").to_string();
        let e = inst.expr.take();
        let dot_coord = arr.ends_with(keytree::MEMBER_SEP);
        let arr = arr.trim_end_matches(keytree::MEMBER_SEP).to_string();
        // `(*p)[i] = v`：解引用基座的下标写，与 `base·[i] = v` 同一条路径——
        // 落成员写槽（`=` 写），不是 `compactarray·set`。后者会把基座当容器整体，
        // 下标被当成「写进容器的值」，于是 `(*r)[0] = "H"` 报「cannot assign int64」。
        let deref_base = arr.starts_with("(*") && arr.ends_with(')');
        // `p[i] = v`（`p` 是 kvlang 指针）：下标作用在指针基座上，是隐式解引用，
        // 与读侧同一判据——写位置不给例外。
        if !deref_base && self.ptr_params.contains(&arr) {
            self.errors.push(Diagnostic {
                pos: Pos { line: 0, col: 0 },
                warn: false,
                info: false,
                message: format!("对指针类型的坐标访问必须显式解引用：写 (*{arr})[i]"),
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        if dot_coord || arr.starts_with('/') {
            // 成员下标写：保留完整成员写槽 `arr·[i,j]`（=` 写），不再经 kvspace·set。
            inst.writes = vec![format!("{arr}{}[{}]", keytree::MEMBER_SEP, idxs)];
            inst.write_types = Vec::new();
            inst.expr = e;
            return;
        }
        let mut args = vec![ast::leaf(&arr)];
        for idx in idxs.split(',').map(|x| x.trim()) {
            if !idx.is_empty() {
                args.push(ast::leaf(idx));
            }
        }
        args.push(e.unwrap_or(ast::leaf("")));
        inst.expr = Some(ast::call("compactarray·set", args));
        inst.writes = vec![arr];
        inst.write_types = Vec::new();
    }

    fn desugar_member_write(&mut self, inst: &mut Instruction) {
        if inst.writes.len() != 1 || !inst.writes[0].contains(keytree::MEMBER_SEP) {
            return;
        }
        let s = inst.writes[0].clone();
        // 路径字面量（/ 开头）是完整 key，不是成员写，勿脱糖——**但含动态键段 `·*k` 的除外**：
        // 那时 `·` 之后是运行期求值的段，必须把成员段展平为显式写槽（写槽名带 `·*k`），
        // runtime ResolveWriteSlot 拼 <frame>+"base·key…" 即成员绝对路径（=` 写）。
        if s.starts_with('/') && !s.contains(&format!("{}*", keytree::MEMBER_SEP)) {
            return;
        }
        // struct 赋值（RHS = struct·new）：浅拷 base+一层成员，lower 为 kvspace·cplist(struct·new→temp, dst)。
        // 不走普通成员写 —— 后者只搬基值，struct 的成员会丢在临时槽。dst 传完整成员槽串。
        let is_struct_new = inst
            .expr
            .as_ref()
            .map(|e| e.op == format!("struct{}new", keytree::MEMBER_SEP))
            .unwrap_or(false);
        if is_struct_new && !s.contains('*') {
            let e = inst.expr.take().unwrap();
            inst.expr = Some(ast::call("kvspace·cplist", vec![e, ast::leaf(&s)]));
            inst.writes = Vec::new();
            inst.write_types = Vec::new();
            return;
        }
        // 成员链写槽 p·a·b = v / p·*k = v：保持写槽原样（含 `·`/`·*`），不再转 kvspace·set。
        // `=` 写（copy 写）落成员绝对路径，runtime 拼路径写值——与读侧 `base·seg…`（=` 读）对称。
        let mut parts = s.split(keytree::MEMBER_SEP);
        let base = parts.next().unwrap_or("").to_string();
        let mut args = vec![ast::leaf(&base)];
        for field in parts {
            if field.starts_with('*') {
                if field.len() == 1 {
                    // Reject malformed `base·*` write slot (defensive fallback after `member_write_segment`).
                    let t = self.peek();
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: false,
                        message: "dynamic member write: expected identifier after '.*'".to_string(),
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                    return;
                }
                args.push(ast::leaf(&field[1..]));
            } else {
                args.push(ast::str_lit(field));
            }
        }
        // base.a.b = v 即成员写 `v -> base·a·b`：写槽保留完整成员串，`=` 写由 runtime
        // ResolveWriteSlot 拼成员绝对路径。不再经 kvspace·set（清理方案：set 死刑）。
        inst.writes = vec![s.clone()];
        inst.write_types = Vec::new();
    }

    /// `[]` / `[v...]` 定长初始化：从写类型 `[N…]T` 取维度，降级为 `array·fill(langtype[, v])`。
    /// 空 `[]` 配定长类型 → 全零；`[v...]` → 全填 v。非定长类型时不改写（沿用普通 array/空数组）。
    fn lower_array_fill(&mut self, inst: &mut Instruction) {
        if inst.writes.len() != 1 {
            return;
        }
        let wt = inst.write_types.first().cloned().unwrap_or_default();
        let fixed = is_fixed_dim_array(&wt);
        let e = match &inst.expr {
            Some(e) => e,
            None => return,
        };
        if e.op == "compactarray·fill" {
            if !fixed {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    warn: false,
                    info: false,
                    message: format!(
                        "fill array [v...] needs a fixed-length array type, got {wt:?}"
                    ),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                return;
            }
            let mut args = vec![ast::str_lit(&wt)];
            args.extend(e.args.iter().cloned());
            inst.expr = Some(ast::call("compactarray·fill", args));
        } else if e.op == "compactarray·new" && e.args.is_empty() && fixed {
            inst.expr = Some(ast::call("compactarray·fill", vec![ast::str_lit(&wt)]));
        }
    }

    fn check_write_type_match(&mut self, inst: &Instruction) {
        let e = match &inst.expr {
            Some(e) => e,
            None => return,
        };
        let expr_is_array = e.op == "compactarray·new";
        let scalar_lit = expr_is_scalar_lit(e);

        for (j, wt) in inst.write_types.iter().enumerate() {
            if wt.is_empty() {
                continue;
            }
            let name = inst.writes.get(j).cloned().unwrap_or_default();
            if !is_array_langtype(wt) && expr_is_array {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("write {name:?} declared scalar {wt} but assigned an array literal — use []{wt} instead"),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            } else if is_array_langtype(wt) && scalar_lit {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("write {name:?} declared {wt} but assigned a scalar literal"),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            } else if !write_type_ok(wt, scalar_lit, expr_is_array) {
                self.push_unknown_type("write", &name, wt, Pos { line: 0, col: 0 });
            }
        }
    }

    /// 「标注收不住它收的字面量」的统一诊断：`int`/`intg64` 这类不是种类名的标注在此落网
    /// （裸名已被 [`super::langtype::expand_struct_refs`] 展开成 `/lib/<name>`，显示时剥回原名）。
    fn push_unknown_type(&mut self, ctx: &str, name: &str, ty: &str, pos: Pos) {
        let disp = ty.strip_prefix("/lib/").unwrap_or(ty);
        self.errors.push(Diagnostic {
            pos,
            message: format!(
                "{ctx} {name:?}: unknown type {disp:?} — 不是已知种类名，\
                 kvlang 无 int/uint/float/num/char 家族简写，数字须带位宽\
                 （int8/int16/int32/int64、uint8/uint16/uint32/uint64、float32/float64），\
                 字符须带编码（char/utf8、char/utf32、char/ascii）"
            ),
            warn: false,
            info: false,
            source: String::new(),
            src_file: String::new(),
            src_name: String::new(),
        });
    }
}

// ── 辅助 ─────────────────────────────────────────────────────────────

const UNARY_PREC: i32 = 150;

fn is_unary_prefix_op(s: &str) -> bool {
    symbol::lookup(s).unary
}

fn is_numeric_literal(v: &str) -> bool {
    if v.is_empty() {
        return false;
    }
    let c0 = v.as_bytes()[0];
    if !c0.is_ascii_digit() {
        return false;
    }
    v.parse::<f64>().is_ok()
}

fn is_float_literal(v: &str) -> bool {
    v.contains('.') || v.contains('e') || v.contains('E')
}

/// 表达式树中任意节点是否为散 key 字面量 `{...}`（parser 产出的 "map·new"）。
fn expr_contains_sparse(e: &Expr) -> bool {
    e.op == "map·new" || e.args.iter().any(expr_contains_sparse)
}

fn attach_comments(st: Stmt, comments: Vec<String>) -> Stmt {
    if comments.is_empty() {
        return st;
    }
    let mut st = st;
    match &mut st {
        Stmt::Instruction(s) => s.comments = comments,
        Stmt::If(s) => s.comments = comments,
        Stmt::For(s) => s.comments = comments,
        Stmt::While(s) => s.comments = comments,
        Stmt::Break(s) => s.comments = comments,
        Stmt::Continue(s) => s.comments = comments,
        Stmt::Scope(s) => s.comments = comments,
    }
    st
}

fn is_array_langtype(t: &str) -> bool {
    t.contains('[')
}

/// 标量字面量：`1`/`1.5`/`true`。字符串字面量恒一维 `[]char/<编码>`（非标量），故排除。
fn expr_is_scalar_lit(e: &Expr) -> bool {
    e.is_leaf()
        && e.lit != ast::LitKind::LitNone
        && e.lit != ast::LitKind::LitNil
        && e.lit != ast::LitKind::LitString
        && e.lit != ast::LitKind::LitRawString
}

/// 字面量右值的标注必须收得住该字面量，**按右值形态分三类**（见 [[文法与合法性]]）：
///   `1`/`1.5`/`true`（标量字面量）→ 标注须是已知种类名或 `any`——类型由字面量自己给出；
///   `[1,2]`（数组字面量）        → 标注须是合法 langtype（`[2]int64`）；
///   `{…}`（结构/容器字面量）     → 标注是 struct 名或 map langtype，**裸名在此合法**（它就是
///                                struct 名，已展开成 `/lib/<name>`）；不判种类名；
///   其余（非字面量右值如 `x:int = y`）无从推断 → 放行。
/// 前两类里裸名 `int`/`intg64` 已由 [`super::langtype::expand_struct_refs`] 变成 `/lib/int`，
/// 既非种类名也非合法形状 → 在此落网。layout 只判种类名，不查 kvspace 里 `/lib/…` 有无原型
/// （存在性/字段一致性归 runtime：`x:int = {}` 放行，runtime 报 "/lib/int is not a struct type"）。
fn write_type_ok(wt: &str, scalar_lit: bool, array_lit: bool) -> bool {
    if scalar_lit {
        super::langtype::is_plain_kind(wt)
    } else if array_lit {
        super::langtype::valid_langtype(wt)
    } else {
        true
    }
}

/// 定长数组类型：`[N]T` / `[d0,d1]T`，方括号内全为正整数（非空、无 `?`）。
/// `[]T`（动态一维）与 `[?,N]T`（含未知维）不算。
fn is_fixed_dim_array(t: &str) -> bool {
    let t = t.trim();
    if !t.starts_with('[') {
        return false;
    }
    let close = match t.find(']') {
        Some(i) => i,
        None => return false,
    };
    let dims = &t[1..close];
    if dims.is_empty() {
        return false;
    }
    dims.split(',')
        .all(|d| !d.trim().is_empty() && d.trim().bytes().all(|c| c.is_ascii_digit()))
}

/// 值容器类型：成员落在兄弟槽 `{key}·` 的类型——map langtype（含 `·`）或 structref（`/` 开头）。
/// 这类值没有可拷贝的单槽值，只能按地址传递（见 spec [[函数]]）。
///
/// 传递方式不参与判断：`*` 已由 `Param::by_ref` 单独承载，`ty` 只剩类型表达式。
fn is_value_container_ty(ty: &str) -> bool {
    ty.split('|')
        .any(|a| a.starts_with('/') || a.contains('·'))
}

fn type_error(_kind: &str) -> String {
    // 列的是**种类名**，容器不在此列：map 写 mapexpr（`[]char/utf8·int64`），
    // struct 写原型路径（`/lib/Point`）——两者都不是种类（见 spec [[langtype语义类型]]）。
    "unknown type — valid: int8/16/32/64, uint8/16/32/64, float32/64, bool, char/utf32, char, any, []T, [2,3]T, [?,N]T, A|B, key·value, /lib/Name".to_string()
}

/// 收集函数体（含嵌套块/分支/循环）里的全部指令，供别名污染的不动点迭代用。
fn collect_body_insts<'a>(body: &'a [Stmt], out: &mut Vec<&'a Instruction>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => out.push(s),
            Stmt::Scope(s) => collect_body_insts(&s.body, out),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    out.push(c);
                }
                collect_body_insts(&s.then_, out);
                collect_body_insts(&s.else_, out);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    out.push(c);
                }
                collect_body_insts(&s.body, out);
            }
            Stmt::For(s) => collect_body_insts(&s.body, out),
            _ => {}
        }
    }
}

fn walk_read_only(
    p: &mut Parser,
    body: &[Stmt],
    ro: &std::collections::HashSet<String>,
    fname: &str,
    check: &dyn Fn(&mut Parser, &Instruction, &std::collections::HashSet<String>, &str),
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => check(p, s, ro, fname),
            Stmt::If(s) => {
                walk_read_only(p, &s.then_, ro, fname, check);
                walk_read_only(p, &s.else_, ro, fname, check);
            }
            Stmt::While(s) => walk_read_only(p, &s.body, ro, fname, check),
            Stmt::For(s) => {
                if ro.contains(&s.var) {
                    p.errors.push(Diagnostic {
                        pos: Pos { line: 1, col: 1 },
                        message: format!("func {fname}: read param {:?} cannot be used as write slot (read params are read-only)", s.var),
                        warn: false,
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                walk_read_only(p, &s.body, ro, fname, check);
            }
            Stmt::Scope(s) => walk_read_only(p, &s.body, ro, fname, check),
            _ => {}
        }
    }
}

/// 类型表达式里是否出现 `any`（含 `*any`、`any...`、shape/mapexpr 的基）。`any` 已删除，
/// 必须显式报 unknown type，不得被隐式展开成 structref `/lib/any`。
fn type_has_any(raw: &str) -> bool {
    raw.split('|').any(|a| {
        let a = a.trim_start_matches(['*', '@']).trim_end_matches("...");
        let base = match a.rfind(']') {
            Some(i) => &a[i + 1..],
            None => a,
        };
        let base = match base.rfind('·') {
            Some(i) => &base[i + '·'.len_utf8()..],
            None => base,
        };
        base == "any"
    })
}

/// 基座是否为「指针形参的裸名」：是则返回名字。
///
/// 新参数模型下形参名是指针，`·`/`[` 直接作用在它上面就是隐式解引用。
/// 判据只认**裸名**（`Expr::is_leaf` 且非字符串字面量）——`(*p)` 已经显式解引用过，
/// 它的基座是解引用表达式，不在此列。
fn bare_ptr_param(p: &Parser, e: &Expr) -> Option<String> {
    if !e.is_leaf() || e.quote != 0 {
        return None;
    }
    p.ptr_params.iter().find(|n| **n == e.val).cloned()
}
