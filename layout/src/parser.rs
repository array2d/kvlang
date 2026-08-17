//! 语法分析（对齐 parser/parser.go、inst.go、stmt.go）：Token 流 → AST。
//!
//! 入口：`parse_code(src) → Result<(File, Vec<Diagnostic>), String>`。

use super::ast::{self, Expr, Func, FuncSig, Instruction, Param, RwirDecl, Stmt};
use super::keytree;
use super::scanner::{scan, Diagnostic, Kind, Pos, Token};
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
    let mut p = Parser {
        tokens: scan(src),
        pos: 0,
        errors: Vec::new(),
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

struct Parser {
    tokens: Vec<Token>,
    pos: usize,
    errors: Vec<Diagnostic>,
}

impl Parser {
    fn peek(&self) -> Token {
        self.tokens.get(self.pos).cloned().unwrap_or_else(|| Token::eof(Pos { line: 0, col: 0 }))
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
            return Token { kind: k, value: String::new(), pos: t.pos, quote: 0 };
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
                let inst = self.parse_inst();
                if let Some(mut inst) = inst {
                    if inst.expr.is_some() {
                        inst.comments = comments;
                        f.top_level_calls.push(inst);
                    }
                } else if self.pos == prev_pos {
                    if self.peek().kind != Kind::EOF {
                        let t = self.peek();
                        self.errors.push(Diagnostic {
                            pos: t.pos,
                            message: format!("unexpected token {} {:?} at top level", t.kind, t.value),
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
                message: format!("package name {name:?} expands to /lib/lib/ — consider a different name"),
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
        self.skip_newlines();
        let mut body: Vec<Stmt> = Vec::new();
        while self.peek().kind != Kind::RBrace && self.peek().kind != Kind::EOF {
            let is_nested_lib = self.peek().kind == Kind::Ident
                && self.peek().value == "lib"
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(2).kind == Kind::LBrace;
            if is_nested_lib {
                self.parse_lib_body(f, &pkg);
                self.skip_newlines();
                continue;
            }
            if self.peek().kind == Kind::Ident && self.peek().value == "rwir" {
                let mut decl = self.parse_rwir_decl();
                decl.pkg = pkg.clone();
                f.rwir_decls.push(decl);
                self.skip_newlines();
                continue;
            } else if self.peek().kind == Kind::Ident && self.peek().value == "rwfunc" {
                let mut func = self.parse_func();
                func.pkg = pkg.clone();
                f.funcs.push(func);
                self.skip_newlines();
                continue;
            }
            match self.parse_stmt() {
                Some(st) => body.push(st),
                None => break,
            }
            self.skip_newlines();
        }
        self.expect(Kind::RBrace);
        if !body.is_empty() {
            f.funcs.push(Func {
                comments: Vec::new(),
                sig: FuncSig { name: "init".to_string(), params: Vec::new(), returns: Vec::new() },
                body,
                pkg: pkg.clone(),
            });
        }
        f.package = prev_pkg;
    }

    fn parse_func(&mut self) -> Func {
        let sig = self.parse_func_sig();
        self.check_param_types(&sig);
        self.check_param_dup(&sig);
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let body = self.parse_body();
        self.expect(Kind::RBrace);
        let func = Func { comments: Vec::new(), sig, body, pkg: String::new() };
        self.check_read_only_params(&func);
        func
    }

    fn parse_rwir_decl(&mut self) -> RwirDecl {
        self.advance(); // consume 'rwir'
        let mut decl = RwirDecl { comments: Vec::new(), sig: FuncSig { name: String::new(), params: Vec::new(), returns: Vec::new() }, pkg: String::new() };
        if self.peek().kind == Kind::Ident {
            decl.sig.name = self.advance().value;
            if self.peek().kind == Kind::Dot {
                decl.sig.name.push_str(&self.advance().value); // .
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
        decl
    }

    fn parse_func_sig(&mut self) -> FuncSig {
        self.advance(); // consume 'rwfunc'
        let mut sig = FuncSig { name: String::new(), params: Vec::new(), returns: Vec::new() };
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
                _ => return sb,
            }
        }
    }

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
            let t = self.peek();
            if t.kind != Kind::Ident && t.kind != Kind::Literal {
                break;
            }
            let mut param = Param { name: self.advance().value, ty: String::new() };
            if self.peek().kind == Kind::Colon {
                self.advance();
                param.ty = self.parse_type();
            }
            params.push(param);
        }
        params
    }

    fn check_param_types(&mut self, sig: &FuncSig) {
        for param in &sig.params {
            if !valid_kindexp(&param.ty) {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("func {}: param {:?}: {} (got {:?})", sig.name, param.name, type_error(&param.ty), param.ty),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        for ret in &sig.returns {
            if !valid_kindexp(&ret.ty) {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("func {}: return value {:?}: {} (got {:?})", sig.name, ret.name, type_error(&ret.ty), ret.ty),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        for param in &sig.params {
            if param.ty.is_empty() {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("func {}: param {:?} has no type annotation — every parameter must declare its type, e.g. {}:int64", sig.name, param.name, param.name),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        for ret in &sig.returns {
            if ret.ty.is_empty() {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("func {}: return value {:?} has no type annotation — every return value must declare its type, e.g. {}:int64", sig.name, ret.name, ret.name),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
    }

    fn check_param_dup(&mut self, sig: &FuncSig) {
        let mut seen = std::collections::HashSet::new();
        for name in sig.param_names() {
            seen.insert(name);
        }
        for ret in &sig.returns {
            if seen.contains(&ret.name) {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("func {}: param {:?} appears in both read-params and write-params — a param is either read-only or write-only, pick one", sig.name, ret.name),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
            seen.insert(ret.name.clone());
        }
    }

    fn check_read_only_params(&mut self, func: &Func) {
        let mut ro = std::collections::HashSet::new();
        for n in func.sig.param_names() {
            ro.insert(n);
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
        let check = |p: &mut Self, inst: &Instruction, ro: &std::collections::HashSet<String>, fname: &str| {
            for (i, w) in inst.writes.iter().enumerate() {
                if w.contains('/') || w.contains('[') || w.contains(keytree::MEMBER_SEP) {
                    continue;
                }
                if let Some(e) = &inst.expr {
                    if e.op == "set" && i == 0 && !e.args.is_empty() && w == &e.args[0].val {
                        continue;
                    }
                }
                if ro.contains(w) {
                    bad(p, w, fname);
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
            let comments = self.collect_leading_comments();
            let t = self.peek();
            if t.kind == Kind::RBrace || t.kind == Kind::EOF {
                break;
            }
            if let Some(st) = self.parse_stmt() {
                let st = attach_comments(st, comments);
                stmts.push(st);
            }
        }
        stmts
    }

    fn parse_stmt(&mut self) -> Option<Stmt> {
        // 块标签检测（优先级最高）
        if self.peek_at(1).kind == Kind::Colon
            && self.peek_at(2).kind != Kind::Ident
            && self.peek_at(2).kind != Kind::LBrack
        {
            return Some(self.parse_block_label());
        }
        match self.peek().kind {
            Kind::If => Some(self.parse_if()),
            Kind::For => Some(self.parse_for()),
            Kind::While => Some(self.parse_while()),
            Kind::Break => {
                self.advance();
                self.eat(Kind::Newline);
                Some(Stmt::Break(ast::BreakStmt { comments: Vec::new() }))
            }
            Kind::Continue => {
                self.advance();
                self.eat(Kind::Newline);
                Some(Stmt::Continue(ast::ContinueStmt { comments: Vec::new() }))
            }
            _ => self.parse_inst().map(Stmt::Instruction),
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
            return Stmt::If(ast::IfStmt { comments: Vec::new(), cond, then_, else_: els });
        }
        if self.peek().kind == Kind::Else {
            self.advance();
            self.skip_newlines_and_comments();
            self.expect(Kind::LBrace);
            let els = self.parse_body();
            self.expect(Kind::RBrace);
            return Stmt::If(ast::IfStmt { comments: Vec::new(), cond, then_, else_: els });
        }
        Stmt::If(ast::IfStmt { comments: Vec::new(), cond, then_, else_: Vec::new() })
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
            return vec![Stmt::If(ast::IfStmt { comments: Vec::new(), cond, then_: body, else_: chain })];
        }
        if self.peek().kind == Kind::Else {
            self.advance();
            self.skip_newlines_and_comments();
            self.expect(Kind::LBrace);
            let els = self.parse_body();
            self.expect(Kind::RBrace);
            return vec![Stmt::If(ast::IfStmt { comments: Vec::new(), cond, then_: body, else_: els })];
        }
        vec![Stmt::If(ast::IfStmt { comments: Vec::new(), cond, then_: body, else_: Vec::new() })]
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
        let mut iter = String::new();
        if self.peek().kind != Kind::RParen && self.peek().kind != Kind::EOF {
            iter = self.advance().value;
        }
        self.expect(Kind::RParen);
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let body = self.parse_body();
        self.expect(Kind::RBrace);
        Stmt::For(ast::ForStmt { comments: Vec::new(), var, iter, body })
    }

    fn parse_while(&mut self) -> Stmt {
        self.advance(); // consume 'while'
        let cond = self.parse_cond_inst();
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let body = self.parse_body();
        self.expect(Kind::RBrace);
        Stmt::While(ast::WhileStmt { comments: Vec::new(), cond, body })
    }

    fn parse_block_label(&mut self) -> Stmt {
        let label = self.advance().value;
        self.advance(); // consume Colon
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let body = self.parse_body();
        self.expect(Kind::RBrace);
        Stmt::Scope(ast::ScopeStmt { comments: Vec::new(), label, body })
    }

    fn parse_cond_inst(&mut self) -> Option<Instruction> {
        self.expect(Kind::LParen);
        let mut inst = Instruction::default();
        inst.expr = self.parse_pratt(0);
        self.expect(Kind::RParen);
        Some(inst)
    }

    // ── 指令级（Pratt） ────────────────────────────────────────────

    fn parse_inst(&mut self) -> Option<Instruction> {
        let mut inst = Instruction::default();

        match self.find_top_level_arrow() {
            Some(v) if v == "<-" || v == "=" => {
                inst.arrow_left = true;
                inst.eq = v == "=";
                let (writes, wtypes) = self.collect_writes_until_arrow();
                inst.writes = writes;
                inst.write_types = wtypes;
                self.advance(); // consume <- / =
                inst.expr = self.parse_pratt(0);
                if inst.writes.len() == 1 && inst.writes[0].contains('[') {
                    let s = inst.writes[0].clone();
                    let br = s.find('[').unwrap_or(s.len());
                    let arr = s[..br].to_string();
                    let idx = s[br + 1..s.len().saturating_sub(1)].to_string();
                    let e = inst.expr.take();
                    inst.expr = Some(ast::call("set", vec![ast::leaf(&arr), ast::leaf(&idx), e.unwrap_or(ast::leaf(""))]));
                    inst.writes = vec![arr];
                    inst.write_types = Vec::new();
                }
                self.desugar_member_write(&mut inst);
            }
            Some(_) => {
                inst.expr = self.parse_pratt(0);
                self.advance(); // consume ->
                let (writes, wtypes) = self.collect_write_list();
                inst.writes = writes;
                inst.write_types = wtypes;
                self.desugar_member_write(&mut inst);
            }
            None => {
                if self.peek().kind == Kind::Ident && self.peek_at(1).kind == Kind::Colon {
                    let (name, typ) = self.parse_write_slot();
                    inst.writes = vec![name];
                    inst.write_types = vec![typ];
                    inst.expr = Some(ast::call("array", Vec::new()));
                } else {
                    inst.expr = self.parse_pratt(0);
                }
            }
        }

        if self.peek().kind == Kind::Comment {
            self.advance();
        }
        self.eat(Kind::Newline);
        self.check_write_type_match(&inst);
        if inst.expr.is_none() && inst.writes.is_empty() {
            return None;
        }
        Some(inst)
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
        loop {
            // 后缀成员访问
            if self.peek().kind == Kind::Dot {
                self.advance(); // consume .
                if self.peek().kind == Kind::Ident && self.peek().value == "*" {
                    self.advance(); // consume *
                    if self.peek().kind == Kind::Ident {
                        let key = self.advance().value;
                        left = ast::call("at", vec![left, ast::leaf(&key)]);
                        continue;
                    }
                }
                if self.peek().kind == Kind::Ident || self.peek().kind == Kind::Literal {
                    let field = self.advance().value;
                    left = ast::call("at", vec![left, ast::str_lit(&field)]);
                    continue;
                }
            }
            // 后缀索引
            if self.peek().kind == Kind::LBrack {
                self.advance();
                let mut indices = Vec::new();
                while self.peek().kind != Kind::RBrack && self.peek().kind != Kind::EOF {
                    if self.eat(Kind::Comma) {
                        continue;
                    }
                    if let Some(idx) = self.parse_pratt(0) {
                        indices.push(idx);
                    }
                }
                self.expect(Kind::RBrack);
                let mut args = vec![left];
                args.extend(indices);
                left = ast::call("at", args);
                continue;
            }
            let t = self.peek();
            if t.kind != Kind::Ident {
                break;
            }
            let prec = Expr::infix_prec(&t.value);
            if prec == 0 || prec <= min_prec {
                break;
            }
            let op = self.advance().value;
            let right = self.parse_pratt(prec)?;
            left = ast::call(&op, vec![left, right]);
        }
        Some(left)
    }

    fn parse_primary_expr(&mut self) -> Option<Expr> {
        let t = self.peek();
        match t.kind {
            Kind::Arrow | Kind::RParen | Kind::RBrack | Kind::Newline | Kind::RBrace | Kind::EOF
            | Kind::Comma | Kind::Comment => return None,
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
            let arg = self.parse_pratt(UNARY_PREC)?;
            return Some(ast::call(&t.value, vec![arg]));
        }

        // 数组字面量
        if t.kind == Kind::LBrack {
            self.advance();
            let mut elems = Vec::new();
            while self.peek().kind != Kind::RBrack && self.peek().kind != Kind::EOF {
                if self.eat(Kind::Comma) {
                    continue;
                }
                if let Some(e) = self.parse_pratt(0) {
                    elems.push(e);
                }
            }
            self.expect(Kind::RBrack);
            return Some(ast::call("array", elems));
        }

        // dict 字面量
        if t.kind == Kind::LBrace {
            let mut j = 1isize;
            while self.peek_at(j).kind == Kind::Newline || self.peek_at(j).kind == Kind::Comment {
                j += 1;
            }
            let is_dict = self.peek_at(j).kind == Kind::RBrace
                || (self.peek_at(j).kind == Kind::Ident
                    && self.peek_at(j + 1).kind == Kind::Arrow
                    && self.peek_at(j + 1).value == "=");
            if !is_dict {
                return None;
            }
            self.advance(); // consume {
            let mut args = Vec::new();
            loop {
                while matches!(self.peek().kind, Kind::Newline | Kind::Comma | Kind::Comment) {
                    self.advance();
                }
                if self.peek().kind == Kind::RBrace || self.peek().kind == Kind::EOF {
                    break;
                }
                if self.peek().kind != Kind::Ident {
                    let t = self.peek();
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: true,
                        message: format!("dict literal: expected member name, got {:?}", t.value),
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                    break;
                }
                let key = self.advance().value;
                if !(self.peek().kind == Kind::Arrow && self.peek().value == "=") {
                    let t = self.peek();
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: true,
                        message: format!("dict literal: expected '=' after {key:?}"),
                        info: false,
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
            return Some(ast::call("dict", args));
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
                let pos = self.tokens.get(self.pos.saturating_sub(1)).map(|t| t.pos).unwrap_or(Pos { line: 0, col: 0 });
                self.errors.push(Diagnostic {
                    pos,
                    message: "return 不接受参数；直接写 return 即可，返回值通过写参零拷贝传递".to_string(),
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
                if let Some(a) = self.parse_pratt(0) {
                    args.push(a);
                }
            }
            self.expect(Kind::RParen);
            return Some(ast::call(&name, args));
        }

        // 点号函数调用 name.name(...) 或 /lib/name.name(...)
        let is_dotted_call = {
            let lhs = self.peek();
            let lhs_ok = lhs.kind == Kind::Ident
                || (lhs.kind == Kind::Literal && !lhs.value.is_empty() && lhs.value.as_bytes()[0] == b'/');
            lhs_ok && self.peek_at(1).kind == Kind::Dot && self.peek_at(2).kind == Kind::Ident
                && {
                    let mut j = 3isize;
                    while self.peek_at(j).kind == Kind::Dot && self.peek_at(j + 1).kind == Kind::Ident {
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
                if let Some(a) = self.parse_pratt(0) {
                    args.push(a);
                }
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
                if let Some(a) = self.parse_pratt(0) {
                    args.push(a);
                }
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
            if t.quote == b'`' {
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
                Kind::Newline | Kind::EOF | Kind::RBrace | Kind::RParen | Kind::RBrack | Kind::Comma
                | Kind::Arrow | Kind::Comment => {}
                _ => {
                    self.errors.push(Diagnostic {
                        pos: next.pos,
                        message: "return cannot take a value — use write-params for output".to_string(),
                        warn: false,
                        info: false,
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
            }
        }
        Some(ast::leaf(&t.value))
    }

    // ── 写槽收集 ─────────────────────────────────────────────────

    fn collect_write_list(&mut self) -> (Vec<String>, Vec<String>) {
        if self.peek().kind == Kind::LParen {
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
            if matches!(t.kind, Kind::Newline | Kind::RBrace | Kind::EOF | Kind::RParen | Kind::Comment) {
                break;
            }
            if t.kind == Kind::Comma {
                self.advance();
                continue;
            }
            let is_path_literal = t.kind == Kind::Literal && !t.value.is_empty() && t.value.as_bytes()[0] == b'/';
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
            if (t.kind == Kind::Ident || is_path_literal) && self.peek_at(1).kind == Kind::Dot {
                let mut w = self.advance().value;
                w.push_str(&self.advance().value); // .
                if self.peek().kind == Kind::Ident && self.peek().value == "*" && self.peek_at(1).kind == Kind::Ident {
                    w.push_str(&self.advance().value); // *
                }
                w.push_str(&self.advance().value);
                writes.push(w);
                wtypes.push(String::new());
            } else {
                let (name, typ) = self.parse_write_slot();
                writes.push(name);
                wtypes.push(typ);
            }
        }
        (writes, wtypes)
    }

    fn parse_write_slot(&mut self) -> (String, String) {
        let name = self.advance().value;
        let mut typ = String::new();
        if self.peek().kind == Kind::Colon {
            self.advance();
            typ = self.parse_type();
            if typ == "int" || typ == "float" {
                let pos = self.peek().pos;
                self.errors.push(Diagnostic {
                    pos,
                    message: format!("ambiguous type {typ:?} in write slot — use int64 or float64 instead"),
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
        let has_paren = self.peek().kind == Kind::LParen;
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
            let is_path_lit = t.kind == Kind::Literal && !t.value.is_empty() && t.value.as_bytes()[0] == b'/';
            if (t.kind == Kind::Ident || is_path_lit) && self.peek_at(1).kind == Kind::Dot {
                let mut w = self.advance().value;
                w.push_str(&self.advance().value); // .
                if self.peek().kind == Kind::Ident && self.peek().value == "*" && self.peek_at(1).kind == Kind::Ident {
                    w.push_str(&self.advance().value); // *
                }
                w.push_str(&self.advance().value);
                writes.push(w);
                wtypes.push(String::new());
                continue;
            }
            if t.kind == Kind::Ident && self.peek_at(1).kind == Kind::LBrack {
                let mut w = self.advance().value;
                w.push_str(&self.advance().value); // [
                let mut depth = 1i32;
                while depth > 0 && self.peek().kind != Kind::EOF && self.peek().kind != Kind::Arrow {
                    if self.peek().kind == Kind::RBrack {
                        depth -= 1;
                    }
                    if self.peek().kind == Kind::LBrack {
                        depth += 1;
                    }
                    w.push_str(&self.advance().value);
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

    fn desugar_member_write(&mut self, inst: &mut Instruction) {
        if inst.writes.len() != 1 || !inst.writes[0].contains(keytree::MEMBER_SEP) {
            return;
        }
        let s = inst.writes[0].clone();
        let dt = s.find(keytree::MEMBER_SEP).unwrap_or(s.len());
        let base = s[..dt].to_string();
        let field = s[dt + 1..].to_string();
        let key = if field.starts_with('*') {
            if field.len() == 1 {
                let t = self.peek();
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: true,
                    message: "dynamic member write: expected identifier after '.*'".to_string(),
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                return;
            }
            ast::leaf(&field[1..])
        } else {
            ast::str_lit(&field)
        };
        let e = inst.expr.take();
        inst.expr = Some(ast::call("set", vec![ast::leaf(&base), key, e.unwrap_or(ast::leaf(""))]));
        inst.writes = vec![base];
        inst.write_types = Vec::new();
    }

    fn check_write_type_match(&mut self, inst: &Instruction) {
        let e = match &inst.expr {
            Some(e) => e,
            None => return,
        };
        let expr_is_array = e.op == "array";
        let expr_is_scalar_lit = e.is_leaf() && e.lit != ast::LitKind::LitNone && e.lit != ast::LitKind::LitNil;

        for (j, wt) in inst.write_types.iter().enumerate() {
            if wt.is_empty() {
                continue;
            }
            let name = inst.writes.get(j).cloned().unwrap_or_default();
            if !is_array_kindexp(wt) && expr_is_array {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("write {name:?} declared scalar {wt} but assigned an array literal — use []{wt} instead"),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            } else if is_array_kindexp(wt) && expr_is_scalar_lit {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    message: format!("write {name:?} declared {wt} but assigned a scalar literal"),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
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

fn valid_kinds() -> &'static [&'static str] {
    &[
        "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float32", "float64",
        "bool", "char/utf32", "char/utf8", "char/ascii", "any",
    ]
}

fn valid_kindexp(t: &str) -> bool {
    let mut t = t;
    while !t.is_empty() {
        match t.as_bytes()[0] {
            b'*' | b'@' => t = &t[1..],
            b'[' => {
                let end = match t.find(']') {
                    Some(e) => e,
                    None => return false,
                };
                if !t[1..end].is_empty() && !valid_dims(&t[1..end]) {
                    return false;
                }
                t = &t[end + 1..];
            }
            _ => return valid_kinds().contains(&t),
        }
    }
    false
}

fn valid_dims(s: &str) -> bool {
    for d in s.split(',') {
        if d.is_empty() {
            return false;
        }
        if !d.bytes().all(|b| b.is_ascii_digit()) {
            return false;
        }
    }
    true
}

fn is_array_kindexp(t: &str) -> bool {
    t.contains('[')
}

fn type_error(kind: &str) -> String {
    if kind == "int" || kind == "float" {
        return "ambiguous type — use int64 or float64 instead".to_string();
    }
    if kind == "string" || kind == "bytes" {
        return "unknown type — use char/utf32 instead".to_string();
    }
    "unknown type — valid: int8/16/32/64, uint8/16/32/64, float32/64, bool, char/utf32, any, []T, [N]T, *T".to_string()
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
