
use super::ast::{
    self, Expr, Field, Func, FuncSig, Instruction, Param, RwirDecl, Stmt, StructDecl,
    MEMBER_READ_OP,
};
use super::keytree;
use super::scanner::{scan_with_diags, Diagnostic, Kind, Pos, Token};
use super::symbol;

pub fn has_errors(diags: &[Diagnostic]) -> bool {
    diags.iter().any(|d| !d.warn && !d.info)
}

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
        position_slot_counter: 0,
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

#[derive(Clone, Copy, PartialEq, Eq)]
enum SignatureSite {
    Rwfunc,
    RwDeclaration,
}

struct Parser {
    tokens: Vec<Token>,
    pos: usize,
    errors: Vec<Diagnostic>,
    
    ptr_params: Vec<String>,
    
    value_params: Vec<String>,
    
    flow: FlowCtx,
    
    position_slot_counter: u32,
}

#[derive(Default)]
struct FlowCtx {
    loop_depth: u32,
    labels: Vec<(String, Pos)>,
    jumps: Vec<(Expr, Pos)>,
}

fn canonical_write_slot_text(text: &str) -> String {
    
    let mut current = text.trim();
    loop {
        if current.len() < 2 || !current.starts_with('(') || !current.ends_with(')') {
            break;
        }
        
        let inner = current[1..current.len() - 1].trim();
        let mut depth = 0i32;
        let mut paired = true;
        for byte in inner.bytes() {
            match byte {
                b'(' => depth += 1,
                b')' => {
                    depth -= 1;
                    if depth < 0 {
                        paired = false;
                        break;
                    }
                }
                _ => {}
            }
        }
        if !paired || depth != 0 || inner.is_empty() {
            break;
        }
        current = inner;
    }
    
    let Some(name) = current.strip_prefix('*') else {
        return text.to_string();
    };
    let mut characters = name.chars();
    let Some(first) = characters.next() else {
        return text.to_string();
    };
    let identifier_start = first == '_' || first.is_alphabetic();
    let identifier_rest = characters.all(|c| c == '_' || c.is_alphanumeric());
    if identifier_start && identifier_rest {
        current.to_string()
    } else {
        text.to_string()
    }
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
                    format!(
                        "{} expects {want} argument(s) — {shape}; got {}",
                        e.op,
                        e.args.len()
                    ),
                );
                continue;
            }
            for &i in label_args {
                let a = &e.args[i];
                if !a.is_leaf() || a.quote != 0 || !defined.contains_key(&a.val) {
                    let shown = if a.is_leaf() {
                        a.val.clone()
                    } else {
                        a.to_string()
                    };
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

    fn next_statement_token_after_colon(&self) -> Kind {
        let mut offset: isize = 2; 
        loop {
            let token = self.peek_at(offset);
            match token.kind {
                Kind::Newline | Kind::Comment => offset += 1,
                _ => return token.kind,
            }
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
        
        self.finish_flow(FlowCtx::default());
        f
    }

    fn parse_lib_body(&mut self, f: &mut ast::File, prefix: &str) {
        self.advance(); 
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
        
        self.ptr_params = sig
            .params
            .iter()
            .chain(sig.returns.iter())
            .filter(|p| !p.by_ref.is_empty() || p.ty.starts_with('/') || p.ty.contains('\u{b7}'))
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
        self.advance(); 
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
                self.advance(); 
                decl.sig.name.push_str(keytree::MEMBER_SEP);
                if self.peek().kind == Kind::Ident {
                    decl.sig.name.push_str(&self.advance().value);
                }
            }
        }
        if self.peek().kind == Kind::LParen {
            self.advance();
            decl.sig.params = self.parse_param_list_side(Kind::RParen, true);
            self.expect(Kind::RParen);
        }
        if self.peek().kind == Kind::Arrow {
            self.advance();
            self.skip_newlines();
            if self.peek().kind == Kind::LParen {
                self.advance();
                decl.sig.returns = self.parse_param_list_side(Kind::RParen, false);
                self.expect(Kind::RParen);
            }
        }
        self.check_param_types(&decl.sig, SignatureSite::RwDeclaration);
        self.check_variadic(&decl.sig);
        self.check_param_dup(&decl.sig);
        decl
    }

    fn parse_struct_decl(&mut self) -> StructDecl {
        self.advance(); 
        let name = self.advance().value; 
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
        self.advance(); 
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
            sig.params = self.parse_param_list_side(Kind::RParen, true);
            self.expect(Kind::RParen);
        }
        if self.peek().kind == Kind::Arrow {
            self.advance();
            self.skip_newlines();
            if self.peek().kind == Kind::LParen {
                self.advance();
                sig.returns = self.parse_param_list_side(Kind::RParen, false);
                self.expect(Kind::RParen);
            } else {
                sig.returns = self.parse_param_list_side(Kind::LBrace, false);
            }
        }
        sig
    }

    fn parse_type(&mut self) -> String {
        let raw = self.parse_type_raw();
        if type_has_any(&raw) {
            self.errors.push(Diagnostic {
                pos: Pos { line: 0, col: 0 },
                message: "unknown type \"any\" — `any` is no longer a type name; for an unconstrained type, write an empty kindexp: signature slot `name:`, Ptr target `*`, or mapexpr value `[k]·`"
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
                
                Kind::Comment => return sb,
                
                Kind::Ident if t.value == "inplace" && depth == 0 && !sb.is_empty() => return sb,
                
                Kind::Ident | Kind::Literal if t.value.starts_with('#') => {
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: false,
                        info: false,
                        message: format!(
                            "parameter name must not use `$`: `{}` is obsolete, write `#{}`",
                            t.value,
                            t.value.trim_start_matches('#')
                        ),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                    self.advance();
                    return sb;
                }
                
                Kind::Literal if t.quote == b'\'' => {
                    sb.push('\'');
                    sb.push_str(&t.value);
                    sb.push('\'');
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
                Kind::Dot if depth == 0 => {
                    
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

    fn parse_param_list_side(&mut self, stop: Kind, is_read_side: bool) -> Vec<Param> {
        let mut params = Vec::new();
        while self.peek().kind != stop && self.peek().kind != Kind::EOF {
            self.skip_newlines();
            if self.peek().kind == stop {
                break;
            }
            if self.eat(Kind::Comma) {
                continue;
            }
            
            let mut inplace = false;
            let at = self.peek();
            if at.kind == Kind::Ident && at.value == "inplace" {
                self.advance();
                if is_read_side {
                    self.errors.push(Diagnostic {
                        pos: at.pos,
                        warn: false,
                        info: false,
                        message: "`inplace` is only allowed on rwfunc or rwir write params, not read params \
                                  — write `inplace T #name` in the write param list, and `T #name` for read params"
                            .to_string(),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                } else {
                    inplace = true;
                }
            }
            
            if self.peek().kind == Kind::Comment {
                let t = self.peek().clone();
                let name = self.take_param_name(&t).unwrap_or_default();
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: false,
                    info: false,
                    message: format!(
                        "parameter must declare a type: `{name}` has an empty type — `#name` is only a comment name; write the type before it (`*int64 #{name}`)"
                    ),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                params.push(Param {
                    name,
                    ty: String::new(),
                    by_ref: String::new(),
                    has_ty: false,
                    inplace,
                });
                continue;
            }
            let start = self.peek();
            
            if !matches!(start.kind, Kind::Ident | Kind::Literal | Kind::LBrack) {
                break;
            }
            
            if self.peek_at(1).kind == Kind::Colon {
                self.advance(); 
                self.advance(); 
                let ty = self.parse_type();
                self.errors.push(Diagnostic {
                    pos: start.pos,
                    warn: false,
                    info: false,
                    message: format!(
                        "parameter declaration must be `*T #name` or `T #name`; legacy `{}:{}` syntax is no longer accepted",
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
                    inplace: false,
                });
                continue;
            }
            
            let ty = self.parse_type();
            let t = self.peek().clone();
            let name = match self.take_param_name(&t) {
                Some(n) => n,
                None => self.reject_param_name_junk(&t, &ty),
            };
            
            let by_ref = ty
                .chars()
                .take_while(|c| *c == '*' || *c == '@')
                .collect::<String>();
            let ty_value = ty.trim_start_matches(['*', '@']).to_string();
            
            if ty_value.is_empty() {
                self.errors.push(Diagnostic {
                    pos: t.pos,
                    warn: false,
                    info: false,
                    message: format!("parameter must declare a type: `{name}` has an empty type"),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
            
            let after = self.peek();
            if after.kind == Kind::Ident && after.value == "inplace" {
                self.advance();
                self.errors.push(Diagnostic {
                    pos: after.pos,
                    warn: false,
                    info: false,
                    message: format!(
                        "`inplace` must precede the parameter type: `inplace {}`; trailing `inplace` after the name is no longer accepted",
                        ty.trim()
                    ),
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
                inplace,
            });
        }
        params
    }

    fn take_param_name(&mut self, t: &Token) -> Option<String> {
        if t.kind != Kind::Comment {
            return None;
        }
        let name = t.value.strip_prefix('#')?;
        if name.is_empty() {
            return None;
        }
        self.advance();
        Some(name.to_string())
    }

    fn reject_param_name_junk(&mut self, t: &Token, ty: &str) -> String {
        let boundary = matches!(
            t.kind,
            Kind::Comma
                | Kind::RParen
                | Kind::Newline
                | Kind::EOF
                | Kind::LBrace
                | Kind::RBrace
                | Kind::Arrow
        );
        if boundary {
            return String::new();
        }
        
        while !matches!(
            self.peek().kind,
            Kind::Comma | Kind::RParen | Kind::Newline | Kind::EOF
        ) {
            self.advance();
        }
        self.errors.push(Diagnostic {
            pos: t.pos,
            warn: false,
            info: false,
            message: format!(
                "parameter declaration must be `{} #name`{}; trailing `{}` after the type is not a `#name` comment",
                ty.trim(),
                if t.value == "inplace" {
                    ", with `inplace` before the type (`inplace <type> #name`)"
                } else {
                    ""
                },
                t.value
            ),
            source: String::new(),
            src_file: String::new(),
            src_name: String::new(),
        });
        String::new()
    }

    fn check_param_types(&mut self, sig: &FuncSig, site: SignatureSite) {
        
        for param in sig.params.iter().chain(sig.returns.iter()) {
            let members: Vec<&str> = param.ty.split('|').collect();
            if members.len() > 1 && members.iter().skip(1).any(|m| m.trim().starts_with('*')) {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    warn: false,
                    info: false,
                    message: format!(
                        "parameter {:?}: `*` applies to the whole type expression, not per union member — write `{}`",
                        param.name,
                        {
                            let stripped: Vec<&str> = members
                                .iter()
                                .map(|m| m.trim().trim_start_matches('*'))
                                .collect();
                            format!("*{} #{}", stripped.join("|"), param.name)
                        }
                    ),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
        }
        
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
                    "func {}: param {:?}: value-copy parameter must be a string, number, or bool — pass containers as `{} #{}`",
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
                        "func {}: return value {:?}: container types must be passed by pointer — write `{:?}`",
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
        let named = |p: &Param| !p.name.is_empty();
        for p in sig.params.iter().filter(|p| named(p)) {
            if !reads.insert(p.name.as_str()) {
                err(self, format!("func {}: duplicate param {:?} in read-params — read-params, write-params and their union must all be name-unique (a name is an address)", sig.name, p.name));
            }
        }
        for r in sig.returns.iter().filter(|r| named(r)) {
            if reads.contains(r.name.as_str()) {
                err(self, format!("func {}: param {:?} appears in both read-params and write-params — a param is either read-only or write-only, pick one", sig.name, r.name));
            } else if !writes.insert(r.name.as_str()) {
                err(self, format!("func {}: duplicate param {:?} in write-params — read-params, write-params and their union must all be name-unique (a name is an address)", sig.name, r.name));
            }
        }
    }

    fn check_read_only_params(&mut self, func: &Func) {
        let mut ro = std::collections::HashSet::new();
        for p in func.sig.params.iter().filter(|p| !p.name.is_empty()) {
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
            
            for w in inst.writes.iter() {
                if w.contains('/') || w.contains('[') || w.contains(keytree::MEMBER_SEP) {
                    continue;
                }
                if ro.contains(w) {
                    bad(p, w, fname);
                    continue;
                }
                
                if let Some(base) = w.strip_prefix(ast::DEREF_OP) {
                    if ro.contains(base) {
                        bad(p, base, fname);
                    }
                }
            }
            
            if let Some(base) = inst
                .writes
                .iter()
                .find(|w| w.contains(keytree::MEMBER_SEP))
                .and_then(|w| w.split(keytree::MEMBER_SEP).next())
                .filter(|b| !b.contains('/'))
            {
                
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

    fn parse_stmt(&mut self) -> Vec<Stmt> {
        
        if self.peek_at(1).kind == Kind::Colon {
            if self.next_statement_token_after_colon() == Kind::LBrace {
                return vec![self.parse_block_label()];
            }
            
            if self.peek_at(2).kind == Kind::Arrow && self.peek_at(2).value == "=" {
                self.errors.push(Diagnostic {
                    pos: self.peek().pos,
                    message: "empty kindexp is only allowed in signature slots (`name:`); \
                              variable annotations must specify a concrete type"
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
        }
        match self.peek().kind {
            Kind::If => vec![self.parse_if()],
            Kind::For => vec![self.parse_for()],
            Kind::While => vec![self.parse_while()],
            Kind::Break | Kind::Continue => {
                let t = self.advance();
                self.eat(Kind::Newline);
                if self.flow.loop_depth == 0 {
                    
                    let kw = if t.kind == Kind::Break {
                        "break"
                    } else {
                        "continue"
                    };
                    self.error_at(
                        t.pos,
                        format!("{kw} outside loop (only allowed inside while/for)"),
                    );
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
        self.advance(); 
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
        self.advance(); 
        self.advance(); 
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
        self.advance(); 
        self.expect(Kind::LParen);
        let mut var = String::new();
        let t = self.peek();
        if t.kind == Kind::Ident || t.kind == Kind::Literal {
            var = self.advance().value;
            if self.peek().kind == Kind::Colon {
                self.advance();
                if self.peek().kind == Kind::Ident {
                    self.advance(); 
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

    fn dispatch_struct_literal(&mut self, inst: &mut Instruction) {
        let is_named_map = inst
            .expr
            .as_ref()
            .is_some_and(|e| e.op == "map·new" && e.args.first().is_some_and(|a| a.quote != 0));
        
        let is_empty_map = inst
            .expr
            .as_ref()
            .is_some_and(|e| e.op == "map·new" && e.args.is_empty());
        if !is_named_map && !is_empty_map {
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
        self.advance(); 
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
        self.advance(); 
        self.skip_newlines_and_comments();
        self.expect(Kind::LBrace);
        let body = self.parse_body();
        self.expect(Kind::RBrace);
        Stmt::Scope(ast::ScopeStmt {
            comments: Vec::new(),
            label,
            body,
            synthetic: false,
        })
    }

    fn parse_cond_inst(&mut self) -> Option<Instruction> {
        self.expect(Kind::LParen);
        let mut inst = Instruction::default();
        inst.expr = self.parse_pratt(0);
        self.expect(Kind::RParen);
        Some(inst)
    }

    fn parse_inst(&mut self) -> Vec<Instruction> {
        let start_pos = self.peek().pos;
        let mut inst = Instruction::default();
        let mut multi: Vec<Instruction> = Vec::new();
        
        let mut expanded: Vec<Instruction> = Vec::new();

        match self.find_top_level_arrow() {
            Some(v) if v == "=" => {
                inst.arrow_left = true;
                let (writes, wtypes) = self.collect_writes_until_arrow();
                self.advance(); 
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
                    self.desugar_member_write(&mut inst, &mut expanded);
                }
            }
            Some(_) => {
                let reads = self.parse_read_list();
                self.advance(); 
                let (writes, wtypes) = self.collect_write_list();
                if reads.len() > 1 {
                    multi = self.expand_multi(reads, &writes, &wtypes, false);
                } else {
                    inst.expr = reads.into_iter().next().flatten();
                    inst.writes = writes;
                    inst.write_types = wtypes;
                    self.dispatch_struct_literal(&mut inst);
                    self.desugar_subscript_write(&mut inst);
                    self.desugar_member_write(&mut inst, &mut expanded);
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
        
        out.append(&mut expanded);
        
        for k in 0..out.len() {
            let i = std::mem::take(&mut out[k]);
            self.check_write_type_match(&i);
            
            let top_legal = i.writes.len() == 1;
            if let Some(e) = &i.expr {
                self.check_sparse_usage(e, top_legal);
                if e.op == "goto" || e.op == "br" {
                    self.flow.jumps.push((e.clone(), start_pos));
                }
            }
            out[k] = i;
        }
        
        for inst in &mut out {
            if inst.pos.line == 0 {
                inst.pos = start_pos;
            }
        }
        out.retain(|i| i.expr.is_some() || !i.writes.is_empty());
        out
    }

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
                pos: Pos::default(),
            };
            self.dispatch_struct_literal(&mut i);
            self.lower_array_fill(&mut i);
            self.desugar_subscript_write(&mut i);
            
            let mut expanded: Vec<Instruction> = Vec::new();
            self.desugar_member_write(&mut i, &mut expanded);
            out.push(i);
            out.append(&mut expanded);
        }
        out
    }

    fn fresh_position_slot(&mut self) -> String {
        self.position_slot_counter += 1;
        format!("_p{}", self.position_slot_counter)
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
        
        let mut chain_base: Option<Expr> = None;
        let mut chain_segs: Vec<Expr> = Vec::new();
        
        let mut dynamic_segments: Vec<Expr> = Vec::new();
        loop {
            
            if self.peek().kind == Kind::Dot {
                
                if chain_base.is_none() && chain_segs.is_empty() {
                    if let Some(name) = bare_ptr_param(self, &left) {
                        
                        let coordinate = matches!(self.peek_at(1).kind, Kind::LBrack);
                        let pos = self.peek().pos;
                        let message = if coordinate {
                            format!("coordinate access on pointer requires explicit dereference: write (*{name})·[i]")
                        } else {
                            format!("member access on pointer requires explicit dereference: write (*{name})·name")
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
                self.advance(); 
                                
                if self.peek().kind == Kind::Ident && self.peek().value == "*" {
                    let star = self.advance(); 
                    if self.peek().kind == Kind::Ident {
                        let key = self.advance().value;
                        if chain_base.is_none() {
                            chain_base = Some(left.clone());
                        }
                        dynamic_segments.push(ast::leaf(&key));
                        continue;
                    }
                    
                    if self.peek().kind != Kind::Literal {
                        self.error_at(
                            star.pos,
                            "dynamic member read: expected a name after `·*` (`base·*k`)"
                                .to_string(),
                        );
                        return None;
                    }
                }
                
                if self.peek().kind == Kind::Ident || self.peek().kind == Kind::Literal {
                    let field = self.advance().value;
                    if chain_base.is_none() {
                        chain_base = Some(left.clone());
                    }
                    chain_segs.push(ast::str_lit(&field));
                    continue;
                }
                
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
            
            if let Some(base) = chain_base.take() {
                if dynamic_segments.is_empty() {
                    let mut args = vec![base];
                    args.append(&mut chain_segs);
                    left = ast::call(MEMBER_READ_OP, args);
                } else {
                    
                    if !chain_segs.is_empty() {
                        self.error_at(
                            Pos { line: 0, col: 0 },
                            "dynamic member key mixed with static segments: not supported this round"
                                .to_string(),
                        );
                        return None;
                    }
                    
                    let mut segments = std::mem::take(&mut dynamic_segments).into_iter();
                    let mut position = match segments.next() {
                        Some(first) => ast::call("pathlang·resolvekey", vec![base, first]),
                        None => base,
                    };
                    for segment in segments {
                        position = ast::call("pathlang·resolvekey", vec![position, segment]);
                    }
                    
                    left = ast::call(ast::DEREF_OP, vec![position]);
                    debug_assert!(
                        left.op == ast::DEREF_OP && left.args.len() == 1,
                        "dynamic-key read must wrap its position in an explicit `*`"
                    );
                }
            }
            
            if self.peek().kind == Kind::LBrack {
                
                if let Some(name) = bare_ptr_param(self, &left) {
                    let pos = self.peek().pos;
                    self.errors.push(Diagnostic {
                        pos,
                        warn: false,
                        info: false,
                        message: format!("coordinate access on pointer requires explicit dereference: write (*{name})[i]"),
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
                
                left = ast::call(
                    if is_path {
                        MEMBER_READ_OP
                    } else {
                        "compactarray·at"
                    },
                    args,
                );
                continue;
            }
            let t = self.peek();
            if t.kind != Kind::Ident {
                break;
            }
            let e = symbol::lookup(&t.value);
            let prec = e.precedence;
            if prec == 0 {
                
                if e.unary && !e.word.is_empty() {
                    let hint = if e.word == "pointer" {
                        " (use × for multiplication)"
                    } else {
                        " (this unary operator cannot be used infix)"
                    };
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: false,
                        message: format!("unary operator {:?} appears after an operand{hint}", t.value),
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
            
            if symbol::lookup(&t.value).word == "pointer" {
                let arg = self.parse_pratt(UNARY_PREC)?;
                self.check_deref_operand(&arg, t.pos);
                return Some(ast::call(ast::DEREF_OP, vec![arg]));
            }
            
            if symbol::lookup(&t.value).word == "bitand" {
                let arg = self.parse_pratt(UNARY_PREC)?;
                let op = format!("pathspace/key{}abs", keytree::MEMBER_SEP);
                if arg.op == ast::MEMBER_READ_OP && arg.args.len() >= 2 {
                    return Some(ast::call(&op, arg.args));
                }
                return Some(ast::call(&op, vec![arg]));
            }
            let arg = self.parse_pratt(UNARY_PREC)?;
            return Some(ast::call(&t.value, vec![arg]));
        }

        if t.kind == Kind::LBrack {
            self.advance();
            let mut elems = Vec::new();
            let mut fill = false;
            while self.peek().kind != Kind::RBrack && self.peek().kind != Kind::EOF {
                if self.eat(Kind::Comma) {
                    continue;
                }
                
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
                self.advance(); 
                path.push_str(keytree::MEMBER_SEP);
                path.push_str(&self.advance().value);
            }
            let mut args = vec![ast::str_lit(&path)];
            self.advance(); 
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
                self.advance(); 
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

        if t.kind == Kind::LBrace {
            let mut j = 1isize;
            while self.peek_at(j).kind == Kind::Newline || self.peek_at(j).kind == Kind::Comment {
                j += 1;
            }
            let named = self.peek_at(j).kind == Kind::Ident
                && self.peek_at(j + 1).kind == Kind::Arrow
                && self.peek_at(j + 1).value == "=";
            self.advance(); 
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
                    
                    let key = self.advance().value;
                    self.advance(); 
                    let Some(val) = self.parse_pratt(0) else {
                        break;
                    };
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
            
            return Some(ast::call("map·new", elems));
        }

        if t.kind == Kind::LParen {
            self.advance();
            let expr = self.parse_pratt(0);
            self.expect(Kind::RParen);
            return expr;
        }

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
                    message: "return takes no arguments; write bare `return` (outputs are passed zero-copy via write params)"
                        .to_string(),
                    warn: false,
                    info: false,
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
            }
            self.advance(); 
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

        let is_dotted_call = {
            let lhs = self.peek();
            let lhs_ok = lhs.kind == Kind::Ident
                || (lhs.kind == Kind::Literal
                    && !lhs.value.is_empty()
                    && lhs.value.as_bytes()[0] == b'/');
            
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
                self.advance(); 
                opcode.push_str(keytree::MEMBER_SEP);
                opcode.push_str(&self.advance().value);
            }
            self.advance(); 
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

        if self.peek().kind == Kind::Ident
            && self.peek_at(1).kind == Kind::Literal
            && !self.peek_at(1).value.is_empty()
            && self.peek_at(1).value.as_bytes()[0] == b'/'
            && self.peek_at(2).kind == Kind::LParen
        {
            let mut opcode = self.advance().value;
            opcode.push_str(&self.advance().value);
            self.advance(); 
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
                message: "`null` is not a valid literal; use `None` for empty references".to_string(),
                warn: false,
                info: false,
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        Some(ast::leaf(&t.value))
    }

    fn collect_write_list(&mut self) -> (Vec<String>, Vec<String>) {
        
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
            
            if t.kind == Kind::Ident
                && (self.peek_at(1).kind == Kind::LBrack
                    || (self.peek_at(1).kind == Kind::Dot && self.peek_at(2).kind == Kind::LBrack))
            {
                let mut w = self.advance().value; 
                if self.peek().kind == Kind::Dot {
                    self.advance();
                    w.push_str(keytree::MEMBER_SEP);
                }
                w.push_str(&self.advance().value); 
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
                
                if self.ptr_params.contains(&w) {
                    self.errors.push(Diagnostic {
                        pos: base_token.pos,
                        warn: false,
                        info: false,
                        message: format!("member access on pointer requires explicit dereference: write `(*{w})·name`"),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                
                while self.peek().kind == Kind::Dot {
                    self.advance(); 
                    w.push_str(keytree::MEMBER_SEP);
                    if !self.member_write_segment(&mut w) {
                        break;
                    }
                }
                writes.push(w);
                wtypes.push(String::new());
                continue;
            }
            
            let is_deref_base = t.kind == Kind::LParen
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(1).value == ast::DEREF_OP
                && self.peek_at(2).kind == Kind::Ident
                && self.peek_at(3).kind == Kind::RParen
                && matches!(self.peek_at(4).kind, Kind::Dot | Kind::LBrack);
            if is_deref_base {
                let mut w = String::from("(*");
                self.advance(); 
                self.advance(); 
                w.push_str(&self.advance().value); 
                self.advance(); 
                w.push(')');
                if self.peek().kind == Kind::LBrack {
                    
                    self.advance(); 
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

    fn check_deref_operand(&mut self, arg: &Expr, pos: Pos) {
        if !arg.is_leaf() {
            return;
        }
        let msg = if arg.quote != 0 {
            "`*` dereference requires a Ptr slot (identifier), not a string literal".to_string()
        } else if self.value_params.contains(&arg.val) {
            
            format!(
                "cannot dereference value-copy parameter: `{}` is read-only and has no writable target",
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

    fn member_write_segment(&mut self, w: &mut String) -> bool {
        let t = self.peek();
        if t.kind == Kind::Ident && t.value == ast::DEREF_OP {
            let next = self.peek_at(1);
            let is_path = next.kind == Kind::Literal && next.value.starts_with('/');
            match next.kind {
                Kind::Ident => {
                    self.advance(); 
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
                    self.advance(); 
                    w.push_str(&self.advance().value);
                    true
                }
                _ => {
                    self.error_at(
                        t.pos,
                        "dynamic member write: expected a name after `·*` (`base·*k`)".to_string(),
                    );
                    self.advance(); 
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

    fn skip_rest_of_slot(&mut self) {
        while !matches!(
            self.peek().kind,
            Kind::Newline | Kind::RBrace | Kind::EOF | Kind::Arrow
        ) {
            self.advance();
        }
    }

    fn parse_write_slot(&mut self) -> (String, String) {
        
        let mut name = String::new();
        
        if self.peek().kind == Kind::LParen
            && self.peek_at(1).kind == Kind::Ident
            && self.peek_at(1).value == ast::DEREF_OP
            && self.peek_at(2).kind == Kind::Ident
            && self.peek_at(3).kind == Kind::RParen
        {
            self.advance(); 
            self.advance(); 
            name.push('(');
            name.push_str(ast::DEREF_OP);
            name.push_str(&self.advance().value); 
            self.advance(); 
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
        
        if !name.starts_with(ast::DEREF_OP) && self.ptr_params.contains(&name) {
            self.errors.push(Diagnostic {
                pos: self.peek().pos,
                warn: false,
                info: false,
                message: format!("writing to pointer parameter requires explicit dereference: write `-> *{name}`"),
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        if name.starts_with(ast::DEREF_OP) && matches!(self.peek().kind, Kind::Dot | Kind::LBrack) {
            self.errors.push(Diagnostic {
                pos: self.peek().pos,
                message:
                    "`*` dereference write slot only takes an identifier (e.g., `*p`); write `(*p)·x` for members or `(*p)[i]` for subscripts"
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
                    message: "empty kindexp is only allowed in signature slots (`name:`); variable and literal annotations must specify a concrete type"
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
            
            let is_deref_base_here = t.kind == Kind::LParen
                && self.peek_at(1).kind == Kind::Ident
                && self.peek_at(1).value == ast::DEREF_OP
                && self.peek_at(2).kind == Kind::Ident
                && self.peek_at(3).kind == Kind::RParen
                && matches!(self.peek_at(4).kind, Kind::Dot | Kind::LBrack);
            if is_deref_base_here {
                let mut w = String::from("(*");
                self.advance(); 
                self.advance(); 
                w.push_str(&self.advance().value); 
                self.advance(); 
                w.push(')');
                if self.peek().kind == Kind::LBrack {
                    self.advance(); 
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
            
            let is_string_member_base = is_quoted_lit
                && self.peek_at(1).kind == Kind::Dot
                && self.peek_at(2).kind != Kind::LBrack;
            if (t.kind == Kind::Ident || is_path_lit || is_string_member_base)
                && self.peek_at(1).kind == Kind::Dot
                && self.peek_at(2).kind != Kind::LBrack
            {
                let mut w = self.advance().value;
                
                if self.ptr_params.contains(&w) {
                    self.errors.push(Diagnostic {
                        pos: t.pos,
                        warn: false,
                        info: false,
                        message: format!("member access on pointer requires explicit dereference: write `(*{w})·name`"),
                        source: String::new(),
                        src_file: String::new(),
                        src_name: String::new(),
                    });
                }
                
                while self.peek().kind == Kind::Dot && self.peek_at(1).kind != Kind::LBrack {
                    self.advance(); 
                    w.push_str(keytree::MEMBER_SEP);
                    if !self.member_write_segment(&mut w) {
                        break;
                    }
                }
                
                let mut ty = String::new();
                if self.peek().kind == Kind::Colon {
                    self.advance();
                    ty = self.parse_type();
                    if ty.is_empty() {
                        self.errors.push(Diagnostic {
                            pos: Pos { line: 0, col: 0 },
                            message: "empty kindexp is only allowed in signature slots (`name:`); member write slot annotations must specify a concrete type"
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
                w.push_str(&self.advance().value); 
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
                let mut w = self.advance().value; 
                self.advance(); 
                w.push_str(keytree::MEMBER_SEP);
                w.push_str(&self.advance().value); 
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

    fn desugar_subscript_write(&mut self, inst: &mut Instruction) {
        if inst.writes.len() != 1 || !inst.writes[0].contains('[') {
            return;
        }
        let s = inst.writes[0].clone();
        let br = s.find('[').unwrap_or(s.len());
        let arr = s[..br].to_string();
        
        let end = s.len().saturating_sub(1).max(br + 1);
        let idxs = s.get(br + 1..end).unwrap_or("").to_string();
        let e = inst.expr.take();
        let dot_coord = arr.ends_with(keytree::MEMBER_SEP);
        let arr = arr.trim_end_matches(keytree::MEMBER_SEP).to_string();
        
        let deref_base = arr.starts_with("(*") && arr.ends_with(')');
        
        let arr_canonical = canonical_write_slot_text(&arr);
        
        if !deref_base && self.ptr_params.contains(&arr) {
            self.errors.push(Diagnostic {
                pos: Pos { line: 0, col: 0 },
                warn: false,
                info: false,
                message: format!("coordinate access on pointer requires explicit dereference: write (*{arr})[i]"),
                source: String::new(),
                src_file: String::new(),
                src_name: String::new(),
            });
        }
        if dot_coord || arr.starts_with('/') {
            
            inst.writes = vec![format!("{arr}{}[{}]", keytree::MEMBER_SEP, idxs)];
            inst.write_types = Vec::new();
            inst.expr = e;
            return;
        }
        
        let mut args = vec![ast::leaf(&arr_canonical)];
        for idx in idxs.split(',').map(|x| x.trim()) {
            if !idx.is_empty() {
                args.push(ast::leaf(idx));
            }
        }
        args.push(e.unwrap_or(ast::leaf("")));
        inst.expr = Some(ast::call("compactarray·set", args));
        inst.writes = vec![arr_canonical];
        inst.write_types = Vec::new();
    }

    fn desugar_member_write(&mut self, inst: &mut Instruction, outputs: &mut Vec<Instruction>) {
        if inst.writes.len() != 1 || !inst.writes[0].contains(keytree::MEMBER_SEP) {
            return;
        }
        let s = inst.writes[0].clone();
        
        if s.starts_with('/') && !s.contains(&format!("{}*", keytree::MEMBER_SEP)) {
            return;
        }
        
        let is_struct_new = inst
            .expr
            .as_ref()
            .map(|e| e.op == format!("struct{}new", keytree::MEMBER_SEP))
            .unwrap_or(false);
        if is_struct_new && !s.contains('*') {
            let e = inst.expr.take().unwrap();
            
            let prototype = e
                .args
                .first()
                .filter(|a| a.quote != 0)
                .map(|a| a.val.clone())
                .unwrap_or_default();
            
            inst.expr = Some(ast::call("pathspace·cplist", vec![e]));
            inst.writes = vec![s.clone()];
            inst.write_types = if prototype.is_empty() {
                Vec::new()
            } else {
                vec![prototype]
            };
            return;
        }
        
        let mut parts = s.split(keytree::MEMBER_SEP);
        let base = parts.next().unwrap_or("").to_string();
        let mut args = vec![ast::leaf(&base)];
        for field in parts {
            if field.starts_with('*') {
                if field.len() == 1 {
                    
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
        
        if s.split(keytree::MEMBER_SEP)
            .skip(1)
            .any(|field| field.starts_with('*'))
        {
            
            if s.split(keytree::MEMBER_SEP)
                .skip(1)
                .any(|field| !field.starts_with('*'))
            {
                self.errors.push(Diagnostic {
                    pos: Pos { line: 0, col: 0 },
                    warn: false,
                    info: false,
                    message: format!(
                        "dynamic member write mixed with static segments ({s}): not supported this round"
                    ),
                    source: String::new(),
                    src_file: String::new(),
                    src_name: String::new(),
                });
                return;
            }
            let value = match inst.expr.take() {
                Some(e) => e,
                None => return,
            };
            let mut position = ast::leaf(&base);
            for field in s.split(keytree::MEMBER_SEP).skip(1) {
                position = ast::call("pathlang·resolvekey", vec![position, ast::leaf(&field[1..])]);
            }
            
            let slot = self.fresh_position_slot();
            inst.expr = Some(position);
            inst.writes = vec![slot.clone()];
            inst.write_types = Vec::new();
            
            let deref_text = format!("*{slot}");
            debug_assert!(
                deref_text.starts_with('*') && deref_text.len() > 1,
                "position slot must be referenced with an explicit `*`: {deref_text:?}"
            );
            outputs.push(Instruction {
                comments: Vec::new(),
                expr: Some(value),
                writes: vec![deref_text],
                write_types: Vec::new(),
                arrow_left: inst.arrow_left,
                operator: false,
                operand_kind: String::new(),
                pos: Pos::default(),
            });
            return;
        }
        
        inst.writes = vec![s.clone()];
        inst.write_types = Vec::new();
    }

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

    fn push_unknown_type(&mut self, ctx: &str, name: &str, ty: &str, pos: Pos) {
        let disp = ty.strip_prefix("/lib/").unwrap_or(ty);
        self.errors.push(Diagnostic {
            pos,
            message: format!(
                "{ctx} {name:?}: unknown type {disp:?} — not a known kind; \
                 pathlang has no int/uint/float/num/char family shorthands: numbers require bit widths \
                 (int8/int16/int32/int64, uint8/uint16/uint32/uint64, float32/float64) and \
                 characters require encodings (char/utf8, char/utf32, char/ascii)"
            ),
            warn: false,
            info: false,
            source: String::new(),
            src_file: String::new(),
            src_name: String::new(),
        });
    }
}

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

fn expr_is_scalar_lit(e: &Expr) -> bool {
    e.is_leaf()
        && e.lit != ast::LitKind::LitNone
        && e.lit != ast::LitKind::LitNil
        && e.lit != ast::LitKind::LitString
        && e.lit != ast::LitKind::LitRawString
}

fn write_type_ok(wt: &str, scalar_lit: bool, array_lit: bool) -> bool {
    if scalar_lit {
        super::langtype::is_plain_kind(wt)
    } else if array_lit {
        super::langtype::valid_langtype(wt)
    } else {
        true
    }
}

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

fn is_value_container_ty(ty: &str) -> bool {
    ty.split('|').any(|a| a.starts_with('/') || a.contains('·'))
}

fn type_error(_kind: &str) -> String {
    
    "unknown type — valid: int8/16/32/64, uint8/16/32/64, float32/64, bool, char/utf32, char, any, []T, [2,3]T, [?,N]T, A|B, key·value, /lib/Name".to_string()
}

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

fn bare_ptr_param(p: &Parser, e: &Expr) -> Option<String> {
    if !e.is_leaf() || e.quote != 0 {
        return None;
    }
    p.ptr_params.iter().find(|n| **n == e.val).cloned()
}
