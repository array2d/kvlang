//! 控制流 lowering + 类型推断 + 特化（对齐 lower/lower.go、infer.go、specialize.go）。

use std::collections::{HashMap, HashSet};

use super::ast::{self, Expr, Func, Instruction, LitKind, Stmt};
use super::scanner::{Diagnostic, Pos};
use super::{builtin, keytree, symbol};

/// 容器类型（stringkeymap / mapexpr）不可用 `[]` 下标访问成员——
/// `[]` 仅限 compact array（shaped langtype，含字符串 `[]char/*`）。
/// 字符串/字符族（含 `[]char/utf32`、`[N]char`）：参与 add 拼接与比较，不做数值特化。
fn is_string_kind(t: &str) -> bool {
    t.contains("char")
}

fn is_container_type(t: &str) -> bool {
    t == "stringkeymap" || t.contains(keytree::MEMBER_SEP)
}

/// `[]` 下标校验：xv·at/xv·set 基座若为容器类型 → 报错，逼用 kvspace·get/kvspace·set/`base·key`。
fn is_container_ty(ty: &str) -> bool {
    // 剥掉间接性前缀 `*`/`@` 再判：签名里 `p:*Point`、`m:*[int64]·int64` 的 `*` 是传递方式，
    // 类型本体仍是值容器——不剥会把这类参数误判为"未定义容器"，成员写全部被拒。
    let t = ty.trim_start_matches(['*', '@']);
    t.contains(keytree::MEMBER_SEP) || t.starts_with('/')
}

/// 容器字面量的写目标必须带**完整 map langtype**（`{memitemkeylangtype}·{memitemvaluelangtype}`）。
/// 容器值的 langtype 就是它（见 [[map容器]]）——kvspace 里没有「langtype=种类名 stringkeymap 的值」；
/// runtime 也绝不据此退化兜底。类型来源二选一：写槽上的 `x:T = {…}` 标注，或签名里参数/返回的声明类型。
/// 二者皆无即 layout 报错，逼代码作者补标注（`{}` 尤其无法推断：空字面量不含任何成员）。
pub fn check_container_typed(fn_: &Func) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    check_container_body(&fn_.body, fn_, &mut diags);
    diags
}

fn check_container_body(body: &[Stmt], fn_: &Func, diags: &mut Vec<Diagnostic>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => check_container_inst(s, fn_, diags),
            Stmt::Scope(s) => check_container_body(&s.body, fn_, diags),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    check_container_inst(c, fn_, diags);
                }
                check_container_body(&s.then_, fn_, diags);
                check_container_body(&s.else_, fn_, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    check_container_inst(c, fn_, diags);
                }
                check_container_body(&s.body, fn_, diags);
            }
            Stmt::For(s) => check_container_body(&s.body, fn_, diags),
            _ => {}
        }
    }
}

fn check_container_inst(s: &Instruction, fn_: &Func, diags: &mut Vec<Diagnostic>) {
    let Some(e) = &s.expr else { return };
    if e.op != "obj" && e.op != "map" {
        return;
    }
    let Some(target) = s.writes.first() else {
        return;
    };
    let ann = s.write_types.first().map(String::as_str).unwrap_or("");
    // structref（`/lib/Name`）字面量是 struct 实例，不是 map 容器，另有 struct·new 路径。
    if ann.starts_with('/') {
        return;
    }
    let declared = if ann.is_empty() {
        fn_.sig
            .params
            .iter()
            .chain(fn_.sig.returns.iter())
            .find(|p| &p.name == target)
            .map(|p| p.ty.as_str())
            .unwrap_or("")
    } else {
        ann
    };
    let msg = if declared.is_empty() {
        format!("container literal `{{…}}` on {target:?} needs a map langtype, e.g. `{target}:[]char/utf8·int64 = {{}}`")
    } else if !super::langtype::valid_langtype(declared) {
        format!("invalid langtype {declared:?} on container literal target {target:?}")
    } else if !declared.contains(keytree::MEMBER_SEP) {
        format!("container type {declared:?} is not a map langtype; expect `{{memitemkeylangtype}}·{{memitemvaluelangtype}}`")
    } else {
        return;
    };
    diags.push(Diagnostic {
        pos: Pos { line: 0, col: 0 },
        warn: false,
        info: false,
        message: msg,
        source: String::new(),
        src_file: String::new(),
        src_name: String::new(),
    });
}

/// 容器成员写的前置条件：base 必须**已声明且类型明确为容器**。
/// 禁止未定义类型的 map —— `m·k = v` 而 m 无显式声明即 error，不做任何自动推断。
pub fn check_map_defined(fn_: &Func) -> Vec<Diagnostic> {
    let mut defined: HashSet<String> = HashSet::new();
    for p in fn_.sig.params.iter().chain(fn_.sig.returns.iter()) {
        if is_container_ty(&p.ty) {
            defined.insert(p.name.clone());
        }
    }
    collect_container_decls(&fn_.body, &mut defined);
    let mut diags = Vec::new();
    check_map_body(&fn_.body, &defined, &mut diags);
    diags
}

/// 收集「带容器类型标注的写槽」声明（`m:T = ...`，T 为 map/structref）。
fn collect_container_decls(body: &[Stmt], out: &mut HashSet<String>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => {
                // 带容器类型标注的写槽（`m:T = ...`）即容器声明；被赋过值的名字一律算已定义
                // （指针别名 `n = node` 后 `n·field` 是合法成员访问，不是未定义 map）。
                for w in &s.writes {
                    out.insert(w.clone());
                }
            }
            Stmt::Scope(s) => collect_container_decls(&s.body, out),
            Stmt::If(s) => {
                collect_container_decls(&s.then_, out);
                collect_container_decls(&s.else_, out);
            }
            Stmt::While(s) => collect_container_decls(&s.body, out),
            Stmt::For(s) => collect_container_decls(&s.body, out),
            _ => {}
        }
    }
}

fn check_map_body(body: &[Stmt], defined: &HashSet<String>, diags: &mut Vec<Diagnostic>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => check_map_inst(s, defined, diags),
            Stmt::Scope(s) => check_map_body(&s.body, defined, diags),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    check_map_inst(c, defined, diags);
                }
                check_map_body(&s.then_, defined, diags);
                check_map_body(&s.else_, defined, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    check_map_inst(c, defined, diags);
                }
                check_map_body(&s.body, defined, diags);
            }
            Stmt::For(s) => check_map_body(&s.body, defined, diags),
            _ => {}
        }
    }
}

fn check_map_base(base: &str, defined: &HashSet<String>, pos: Pos, diags: &mut Vec<Diagnostic>) {
    if base.is_empty() || base.starts_with("/lib") || defined.contains(base) {
        return;
    }
    diags.push(Diagnostic {
        pos,
        warn: false,
        info: false,
        message: format!(
            "member write on undefined container {base:?} — declare it first with an explicit type, e.g. `{base}:[]char/utf8·int64 = {{}}`"
        ),
        source: String::new(),
        src_file: String::new(),
        src_name: String::new(),
    });
}

fn check_map_inst(s: &Instruction, defined: &HashSet<String>, diags: &mut Vec<Diagnostic>) {
    for w in &s.writes {
        if let Some(i) = w.find(keytree::MEMBER_SEP) {
            if i > 0 {
                check_map_base(&w[..i], defined, Pos { line: 0, col: 0 }, diags);
            }
        }
    }
    // kvspace·set(base, key, val) 成员形
    if let Some(e) = &s.expr {
        if e.op == "kvspace·set" && e.args.len() >= 3 && !e.args[0].val.contains('/') {
            check_map_base(&e.args[0].val, defined, Pos { line: 0, col: 0 }, diags);
        }
    }
}

pub fn check_container_subscript(fn_: &Func) -> Vec<Diagnostic> {
    let tm = infer_types(fn_);
    let addr: Vec<&str> = fn_
        .sig
        .params
        .iter()
        .chain(fn_.sig.returns.iter())
        .map(|p| p.name.as_str())
        .collect();
    let mut diags = Vec::new();
    check_subscript_body(&fn_.body, &tm, &addr, &mut diags);
    diags
}

fn check_subscript_body(
    body: &[Stmt],
    tm: &HashMap<String, String>,
    addr: &[&str],
    diags: &mut Vec<Diagnostic>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => check_subscript_inst(s, tm, addr, diags),
            Stmt::Scope(s) => check_subscript_body(&s.body, tm, addr, diags),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    check_subscript_inst(c, tm, addr, diags);
                }
                check_subscript_body(&s.then_, tm, addr, diags);
                check_subscript_body(&s.else_, tm, addr, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    check_subscript_inst(c, tm, addr, diags);
                }
                check_subscript_body(&s.body, tm, addr, diags);
            }
            Stmt::For(s) => check_subscript_body(&s.body, tm, addr, diags),
            _ => {}
        }
    }
}

fn check_subscript_inst(
    inst: &Instruction,
    tm: &HashMap<String, String>,
    addr: &[&str],
    diags: &mut Vec<Diagnostic>,
) {
    if let Some(e) = &inst.expr {
        check_subscript_expr(e, tm, addr, diags);
    }
}

fn check_subscript_expr(
    e: &Expr,
    tm: &HashMap<String, String>,
    addr: &[&str],
    diags: &mut Vec<Diagnostic>,
) {
    if (e.op == "xv·at" || e.op == "xv·set") && !e.args.is_empty() {
        let base = &e.args[0];
        if base.is_leaf() && !addr.contains(&base.val.as_str()) {
            if let Some(t) = tm.get(&base.val) {
                let msg = if is_container_type(t) {
                    Some(format!(
                        "`[]` 下标不能用于容器 `{}`（类型 {}）；容器成员访问用 kvspace·get/kvspace·set 或 `{}·key`，`[]` 仅限 compact array",
                        base.val, t, base.val
                    ))
                } else if t.starts_with('*') {
                    // 指针不是数组：`[]` 不对指针隐式解引用（见 spec [[ptr]]）。
                    Some(format!(
                        "`[]` 下标不能用于指针 `{}`（类型 {}）；先解引用 `*{}` 再下标",
                        base.val, t, base.val
                    ))
                } else {
                    None
                };
                if let Some(message) = msg {
                    diags.push(Diagnostic {
                        pos: Pos { line: 0, col: 0 },
                        message,
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
    for a in &e.args {
        check_subscript_expr(a, tm, addr, diags);
    }
}

// ── 控制流 lowering（续体传递风格） ──────────────────────────────────

struct LoopCtx {
    break_label: String,
    continue_label: String,
}

struct LabelGen {
    n: i32,
}

impl LabelGen {
    fn next(&mut self, prefix: &str) -> String {
        self.n += 1;
        format!("_{prefix}_{}", self.n)
    }
    fn tmp(&mut self) -> String {
        self.n += 1;
        format!("_{}", self.n)
    }
}

/// 将函数体中 if/while 控制流降级为 ScopeStmt + br/goto，展开复合表达式。
pub fn lower_func(fn_: &Func) -> Func {
    let tm = infer_types(fn_);
    let mut lg = LabelGen { n: 0 };
    // 收集已有 br/goto 的跳转目标：这些 label 已可达，lower 时不再补 fall-through goto（幂等）。
    let mut targets = HashSet::new();
    collect_goto_targets(&fn_.body, &mut targets);
    let body = terminate(lower_body(&fn_.body, &mut lg, None, &tm, &targets));
    Func {
        comments: Vec::new(),
        sig: fn_.sig.clone(),
        body,
        pkg: String::new(),
    }
}

fn return_inst() -> Stmt {
    Stmt::Instruction(Instruction {
        comments: Vec::new(),
        expr: Some(ast::leaf("return")),
        writes: Vec::new(),
        write_types: Vec::new(),
        arrow_left: false,
    })
}

/// Append `return` on open paths. Layout panics if a block is still unterminated.
fn terminate(mut body: Vec<Stmt>) -> Vec<Stmt> {
    for st in &mut body {
        if let Stmt::Scope(s) = st {
            s.body = terminate(std::mem::take(&mut s.body));
        }
    }
    match body.last() {
        None => vec![return_inst()],
        Some(Stmt::Instruction(s)) if is_terminator(s) => body,
        Some(Stmt::Scope(_)) => body,
        _ => {
            body.push(return_inst());
            body
        }
    }
}

/// 递归收集 br/goto 的跳转目标 label（含嵌套 scope/if/while/for 体）。
fn collect_goto_targets(stmts: &[Stmt], targets: &mut HashSet<String>) {
    for st in stmts {
        match st {
            Stmt::Instruction(s) => {
                if let Some(e) = &s.expr {
                    match e.op.as_str() {
                        "goto" => {
                            if let Some(a) = e.args.first() {
                                targets.insert(a.val.clone());
                            }
                        }
                        "br" => {
                            if e.args.len() >= 3 {
                                targets.insert(e.args[1].val.clone());
                                targets.insert(e.args[2].val.clone());
                            }
                        }
                        _ => {}
                    }
                }
            }
            Stmt::Scope(s) => collect_goto_targets(&s.body, targets),
            Stmt::If(s) => {
                collect_goto_targets(&s.then_, targets);
                collect_goto_targets(&s.else_, targets);
            }
            Stmt::While(s) => collect_goto_targets(&s.body, targets),
            Stmt::For(s) => collect_goto_targets(&s.body, targets),
            _ => {}
        }
    }
}

fn lower_body(
    stmts: &[Stmt],
    lg: &mut LabelGen,
    lc: Option<&LoopCtx>,
    tm: &HashMap<String, String>,
    targets: &HashSet<String>,
) -> Vec<Stmt> {
    if stmts.is_empty() {
        return Vec::new();
    }
    let mut preamble: Vec<Stmt> = Vec::new();
    for (i, st) in stmts.iter().enumerate() {
        match st {
            Stmt::Instruction(s) => {
                // 散 key 数组字面量 `base = {v0, v1, ...}` → 逐元素散 key 写入。
                if s.writes.len() == 1 {
                    if let Some(e) = &s.expr {
                        if e.op == "map" {
                            preamble.extend(expand_sparse(&s.writes[0], e, &s.write_types, lg));
                            continue;
                        }
                    }
                }
                let need_flatten = match &s.expr {
                    Some(e) => !e.is_leaf() && !all_args_leaf(e),
                    None => false,
                };
                if need_flatten {
                    let (flat, extra) = flatten_nested_calls(s, lg);
                    preamble.extend(extra);
                    preamble.push(Stmt::Instruction(flat));
                    continue;
                }
                preamble.push(st.clone());
            }
            Stmt::If(s) => {
                let cont = lower_body(&stmts[i + 1..], lg, lc, tm, targets);
                return lower_if_with_cont(&preamble, s, cont, lg, lc, tm, targets);
            }
            Stmt::While(s) => {
                let cont = lower_body(&stmts[i + 1..], lg, lc, tm, targets);
                return lower_while_with_cont(&preamble, s, cont, lg, lc, tm, targets);
            }
            Stmt::Scope(s) => {
                let mut s = s.clone();
                s.body = lower_body(&s.body, lg, lc, tm, targets);
                if !preamble_ends_with_terminator(&preamble) && !targets.contains(&s.label) {
                    preamble.push(goto_label(&s.label));
                }
                let mut out = preamble;
                out.push(Stmt::Scope(s));
                out.extend(lower_body(&stmts[i + 1..], lg, lc, tm, targets));
                return out;
            }
            Stmt::Break(_) => {
                let lc = lc.expect("break outside loop");
                preamble.push(goto_label(&lc.break_label));
                return preamble;
            }
            Stmt::Continue(_) => {
                let lc = lc.expect("continue outside loop");
                preamble.push(goto_label(&lc.continue_label));
                return preamble;
            }
            Stmt::For(s) => {
                let cont = lower_body(&stmts[i + 1..], lg, lc, tm, targets);
                return lower_for_with_cont(&preamble, s, cont, lg, lc, tm, targets);
            }
        }
    }
    preamble
}

fn lower_if_with_cont(
    pre: &[Stmt],
    s: &ast::IfStmt,
    cont: Vec<Stmt>,
    lg: &mut LabelGen,
    lc: Option<&LoopCtx>,
    tm: &HashMap<String, String>,
    targets: &HashSet<String>,
) -> Vec<Stmt> {
    let (cond_eval, cond_slot) = eval_cond(s.cond.as_ref(), lg);
    let if_label = lg.next("if");
    let then_label = lg.next("then");
    let else_label = lg.next("else");
    let merge_label = lg.next("merge");

    let mut cond_body = cond_eval;
    cond_body.push(br_inst(&cond_slot, &then_label, &else_label));

    let then_ = inject_goto(lower_body(&s.then_, lg, lc, tm, targets), &merge_label);
    let else_ = inject_goto(lower_body(&s.else_, lg, lc, tm, targets), &merge_label);
    let (then_insts, mut then_blocks) = split_insts_and_blocks(then_);
    let (else_insts, mut else_blocks) = split_insts_and_blocks(else_);
    let (cont_insts, cont_blocks) = split_insts_and_blocks(cont);

    inject_goto_blocks(&mut then_blocks, &merge_label);
    inject_goto_blocks(&mut else_blocks, &merge_label);

    let mut result = pre.to_vec();
    result.push(goto_label(&if_label));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: if_label,
        body: cond_body,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: then_label,
        body: then_insts,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: else_label,
        body: else_insts,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: merge_label,
        body: cont_insts,
    }));
    result.extend(then_blocks);
    result.extend(else_blocks);
    result.extend(cont_blocks);
    result
}

fn lower_while_with_cont(
    pre: &[Stmt],
    s: &ast::WhileStmt,
    cont: Vec<Stmt>,
    lg: &mut LabelGen,
    _lc: Option<&LoopCtx>,
    tm: &HashMap<String, String>,
    targets: &HashSet<String>,
) -> Vec<Stmt> {
    let (cond_eval, cond_slot) = eval_cond(s.cond.as_ref(), lg);
    let cond_label = lg.next("while");
    let body_label = lg.next("do");
    let exit_label = lg.next("exit");

    let mut cond_body = cond_eval;
    cond_body.push(br_inst(&cond_slot, &body_label, &exit_label));

    let body_lc = LoopCtx {
        break_label: exit_label.clone(),
        continue_label: cond_label.clone(),
    };
    let body_ = inject_goto(
        lower_body(&s.body, lg, Some(&body_lc), tm, targets),
        &cond_label,
    );
    let (body_insts, mut body_blocks) = split_insts_and_blocks(body_);
    inject_goto_blocks(&mut body_blocks, &cond_label);
    let (cont_insts, cont_blocks) = split_insts_and_blocks(cont);

    let mut result = pre.to_vec();
    result.push(goto_label(&cond_label));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: cond_label,
        body: cond_body,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: body_label,
        body: body_insts,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: exit_label,
        body: cont_insts,
    }));
    result.extend(body_blocks);
    result.extend(cont_blocks);
    result
}

fn lower_for_with_cont(
    pre: &[Stmt],
    s: &ast::ForStmt,
    cont: Vec<Stmt>,
    lg: &mut LabelGen,
    _lc: Option<&LoopCtx>,
    tm: &HashMap<String, String>,
    targets: &HashSet<String>,
) -> Vec<Stmt> {
    let init_label = lg.next("for_init");
    let cond_label = lg.next("for_cond");
    let body_label = lg.next("for_body");
    let exit_label = lg.next("for_exit");

    let idx_slot = lg.tmp();
    let cond_slot = lg.tmp();
    let len_slot = lg.tmp();
    let key_slot = lg.tmp();
    // 容器源（stringkeymap/struct）用 kvspace·listlen/kvspace·listat/kvspace·get 遍历，compact 数组源用
    // ndarray·numel/xv·at（见 spec 控制流脱糖）。判据看**声明的类型**：map 的 kindexpr 恒含
    // `key·value` 的那个 `·`（`[int64]·int64`、`[]char/utf8·int64`），compact 数组不含。
    let is_obj = s.iter.op == "obj"
        || s.iter.op == "map"
        || (s.iter.is_leaf()
            && matches!(tm.get(&s.iter.val),
                Some(t) if t == "stringkeymap" || t.contains(keytree::MEMBER_SEP)));

    // 迭代源：裸标识符直接原地遍历；表达式（如数组字面量）先物化到临时槽。
    let mut init_body = Vec::new();
    let iter_slot = if s.iter.is_leaf() {
        s.iter.val.clone()
    } else if s.iter.op == "map" {
        let slot = lg.tmp();
        // for-in 源是裸字面量：无写目标可标注，就地推 map langtype（元素类型必须唯一且可推）。
        let wty = infer_sparse_type(&s.iter, tm);
        init_body.extend(expand_sparse(&slot, &s.iter, &wty, lg));
        slot
    } else {
        let slot = lg.tmp();
        init_body.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(s.iter.clone()),
            writes: vec![slot.clone()],
            write_types: Vec::new(),
            arrow_left: true,
        }));
        slot
    };
    init_body.push(make_copy_inst("-1", &idx_slot));
    if is_obj {
        init_body.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call("kvspace·listlen", vec![ast::leaf(&iter_slot)])),
            writes: vec![len_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
        }));
    } else {
        init_body.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call("ndarray·numel", vec![ast::leaf(&iter_slot)])),
            writes: vec![len_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
        }));
    }
    init_body.push(goto_label(&cond_label));

    let add_inst = Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("+", vec![ast::leaf(&idx_slot), ast::leaf("1")])),
        writes: vec![idx_slot.clone()],
        write_types: Vec::new(),
        arrow_left: false,
    };
    let lt_inst = Instruction {
        comments: Vec::new(),
        expr: Some(ast::call(
            "<",
            vec![ast::leaf(&idx_slot), ast::leaf(&len_slot)],
        )),
        writes: vec![cond_slot.clone()],
        write_types: Vec::new(),
        arrow_left: false,
    };
    let cond_body = vec![
        Stmt::Instruction(add_inst),
        Stmt::Instruction(lt_inst),
        br_inst(&cond_slot, &body_label, &exit_label),
    ];

    let body_lc = LoopCtx {
        break_label: exit_label.clone(),
        continue_label: cond_label.clone(),
    };
    let body_inner = lower_body(&s.body, lg, Some(&body_lc), tm, targets);
    let mut body_insts = Vec::new();
    if is_obj {
        body_insts.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call(
                "kvspace·listat",
                vec![ast::leaf(&iter_slot), ast::leaf(&idx_slot)],
            )),
            writes: vec![key_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
        }));
        body_insts.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call(
                "kvspace·get",
                vec![ast::leaf(&iter_slot), ast::leaf(&key_slot)],
            )),
            writes: vec![s.var.clone()],
            write_types: Vec::new(),
            arrow_left: false,
        }));
    } else {
        body_insts.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call(
                "xv·at",
                vec![ast::leaf(&iter_slot), ast::leaf(&idx_slot)],
            )),
            writes: vec![s.var.clone()],
            write_types: Vec::new(),
            arrow_left: false,
        }));
    }
    body_insts.extend(body_inner);
    let body_insts = inject_goto(body_insts, &cond_label);

    let (body_insts_only, mut body_blocks) = split_insts_and_blocks(body_insts);
    inject_goto_blocks(&mut body_blocks, &cond_label);
    let (cont_insts, cont_blocks) = split_insts_and_blocks(cont);

    let mut result = pre.to_vec();
    result.push(goto_label(&init_label));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: init_label,
        body: init_body,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: cond_label,
        body: cond_body,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: body_label,
        body: body_insts_only,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        label: exit_label,
        body: cont_insts,
    }));
    result.extend(body_blocks);
    result.extend(cont_blocks);
    result
}

// ── 展开嵌套调用 ────────────────────────────────────────────────────

fn flatten_nested_calls(inst: &Instruction, lg: &mut LabelGen) -> (Instruction, Vec<Stmt>) {
    let mut extra = Vec::new();
    let mut flat = inst.clone();
    flat.expr = inst.expr.as_ref().map(|e| flatten_expr(e, lg, &mut extra));
    (flat, extra)
}

fn flatten_expr(e: &Expr, lg: &mut LabelGen, extra: &mut Vec<Stmt>) -> Expr {
    if e.is_leaf() {
        return e.clone();
    }
    let mut new_args = Vec::with_capacity(e.args.len());
    for arg in &e.args {
        if !arg.is_leaf() {
            let tmp = lg.tmp();
            let flat_expr = flatten_expr(arg, lg, extra);
            extra.push(Stmt::Instruction(Instruction {
                comments: Vec::new(),
                expr: Some(flat_expr),
                writes: vec![tmp.clone()],
                write_types: Vec::new(),
                arrow_left: false,
            }));
            new_args.push(ast::leaf(&tmp));
        } else {
            new_args.push(arg.clone());
        }
    }
    Expr {
        op: e.op.clone(),
        args: new_args,
        val: String::new(),
        quote: e.quote,
        lit: LitKind::LitNone,
    }
}

// ── 助手 ─────────────────────────────────────────────────────────────

fn br_inst(cond: &str, t_label: &str, f_label: &str) -> Stmt {
    Stmt::Instruction(Instruction {
        comments: Vec::new(),
        expr: Some(ast::call(
            "br",
            vec![ast::leaf(cond), ast::leaf(t_label), ast::leaf(f_label)],
        )),
        writes: Vec::new(),
        write_types: Vec::new(),
        arrow_left: false,
    })
}

fn goto_label(label: &str) -> Stmt {
    Stmt::Instruction(Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("goto", vec![ast::leaf(label)])),
        writes: Vec::new(),
        write_types: Vec::new(),
        arrow_left: false,
    })
}

/// 容器字面量的 map langtype 推断（仅用于无写目标可标注的场合，如 for-in 源）：
/// 位置型 `{v0,v1,…}` 键为 1 元坐标 `[int64]`，命名型 `{k=v}` 键为字符串 `[]char/utf8`；
/// 值类型取首个元素的推断类型。推不出（元素类型未知）则空 → runtime 直接 fatal 暴露。
fn infer_sparse_type(e: &Expr, tm: &HashMap<String, String>) -> Vec<String> {
    let (keylt, val) = if e.op == "map" {
        ("[int64]", e.args.first())
    } else {
        ("[]char/utf8", e.args.get(1))
    };
    // 成员值类型：char 元素在容器里按字符串数组存，故 `char/*` 上提为 `[]char/*`。
    let vt = match val.map(|a| slot_type(&a.val, tm)).unwrap_or_default() {
        t if t.starts_with("char/") => format!("[]{t}"),
        t => t,
    };
    if vt.is_empty() {
        Vec::new()
    } else {
        vec![format!("{keylt}{}{vt}", keytree::MEMBER_SEP)]
    }
}

/// 散 key 数组字面量（parser 产出的 "map"）→ 单条 `map(v0, v1, ...) -> base`，
/// 运行时 map builtin 创建 map 根（kind=map）+ 子键 `base/[i]`（相对 key，方括号索引串）。
fn expand_sparse(base: &str, e: &Expr, wtypes: &[String], _lg: &mut LabelGen) -> Vec<Stmt> {
    let inst = Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("map", e.args.clone())),
        writes: vec![base.to_string()],
        // 写类型**必须原样带过来**：容器字面量的 map langtype 就靠它落到写槽
        // （`nums:[int64]·int64 = {…}`；见 [[map容器]]）。曾在此清空，runtime 无从得知容器类型。
        write_types: wtypes.to_vec(),
        arrow_left: true,
    };
    vec![Stmt::Instruction(inst)]
}

fn make_copy_inst(val: &str, dest: &str) -> Stmt {
    Stmt::Instruction(Instruction {
        comments: Vec::new(),
        expr: Some(ast::leaf(val)),
        writes: vec![dest.to_string()],
        write_types: Vec::new(),
        arrow_left: false,
    })
}

fn split_insts_and_blocks(stmts: Vec<Stmt>) -> (Vec<Stmt>, Vec<Stmt>) {
    let mut insts = Vec::new();
    let mut blocks = Vec::new();
    for s in stmts {
        if matches!(s, Stmt::Scope(_)) {
            blocks.push(s);
        } else {
            insts.push(s);
        }
    }
    (insts, blocks)
}

fn inject_goto_blocks(stmts: &mut [Stmt], label: &str) {
    for s in stmts {
        if let Stmt::Scope(b) = s {
            b.body = inject_goto(std::mem::take(&mut b.body), label);
        }
    }
}

fn inject_goto(mut body: Vec<Stmt>, label: &str) -> Vec<Stmt> {
    let g = goto_label(label);
    if body.is_empty() {
        return vec![g];
    }
    let last = body.len() - 1;
    let mut needs_push = true;
    match &mut body[last] {
        Stmt::Instruction(s) => {
            if is_terminator(s) {
                needs_push = false;
            }
        }
        Stmt::Scope(s) => {
            s.body = inject_goto(std::mem::take(&mut s.body), label);
            needs_push = false;
        }
        _ => {}
    }
    if needs_push {
        body.push(g);
    }
    body
}

fn is_terminator(s: &Instruction) -> bool {
    match &s.expr {
        Some(e) if e.is_leaf() => e.val == "return",
        Some(e) => matches!(e.op.as_str(), "return" | "goto" | "br"),
        None => false,
    }
}

fn preamble_ends_with_terminator(preamble: &[Stmt]) -> bool {
    match preamble.last() {
        Some(Stmt::Instruction(s)) => is_terminator(s),
        _ => false,
    }
}

fn all_args_leaf(e: &Expr) -> bool {
    e.args.iter().all(|a| a.is_leaf())
}

fn eval_cond(cond: Option<&Instruction>, lg: &mut LabelGen) -> (Vec<Stmt>, String) {
    let cond = match cond {
        Some(c) => c,
        None => return (Vec::new(), String::new()),
    };
    if is_cond_simple_slot(cond) {
        let v = cond
            .expr
            .as_ref()
            .map(|e| e.val.clone())
            .unwrap_or_default();
        return (Vec::new(), v);
    }
    let mut insts = Vec::new();
    let mut flat = cond.clone();
    if let Some(e) = &cond.expr {
        if !all_args_leaf(e) {
            let (f, extra) = flatten_nested_calls(cond, lg);
            flat = f;
            insts.extend(extra);
        }
    }
    let slot = lg.tmp();
    flat.writes = vec![slot.clone()];
    flat.write_types = Vec::new();
    insts.push(Stmt::Instruction(flat));
    (insts, slot)
}

fn is_cond_simple_slot(inst: &Instruction) -> bool {
    inst.expr.as_ref().map(|e| e.is_leaf()).unwrap_or(false) && inst.writes.is_empty()
}

// ── 类型推断（对齐 lower/infer.go） ──────────────────────────────────

/// 声明类型表：仅签名与源码写槽注解。推断类型不是声明（[[赋值转换]]），
/// 故不得进入指令槽的 langtype——否则动态变量会被推断出的静态类型锁死。
pub fn declared_types(fn_: &Func) -> HashMap<String, String> {
    let mut d = HashMap::new();
    for p in fn_.sig.params.iter().chain(fn_.sig.returns.iter()) {
        if !p.ty.is_empty() {
            d.insert(p.name.clone(), p.ty.clone());
        }
    }
    collect_declared(&fn_.body, &mut d);
    d
}

pub fn infer_types(fn_: &Func) -> HashMap<String, String> {
    let mut tm = HashMap::new();
    for p in &fn_.sig.params {
        if !p.ty.is_empty() {
            tm.insert(p.name.clone(), p.ty.clone());
        }
    }
    for p in &fn_.sig.returns {
        if !p.ty.is_empty() {
            tm.insert(p.name.clone(), p.ty.clone());
        }
    }
    infer_body(&fn_.body, &mut tm);
    tm
}

fn infer_body(body: &[Stmt], tm: &mut HashMap<String, String>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => infer_inst(s, tm),
            Stmt::Scope(s) => infer_body(&s.body, tm),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    infer_inst(c, tm);
                }
                infer_body(&s.then_, tm);
                infer_body(&s.else_, tm);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    infer_inst(c, tm);
                }
                infer_body(&s.body, tm);
            }
            Stmt::For(s) => infer_body(&s.body, tm),
            _ => {}
        }
    }
}

fn infer_inst(inst: &Instruction, tm: &mut HashMap<String, String>) {
    let e = match &inst.expr {
        Some(e) => e,
        None => return,
    };
    if inst.writes.is_empty() {
        return;
    }
    for (j, w) in inst.writes.iter().enumerate() {
        if j < inst.write_types.len() && !inst.write_types[j].is_empty() {
            tm.entry(w.clone())
                .or_insert_with(|| inst.write_types[j].clone());
        }
    }
    if e.is_leaf() && e.lit != LitKind::LitNone {
        let inferred = lit_to_type(e.lit);
        if !inferred.is_empty() {
            for w in &inst.writes {
                if w.is_empty() || w.starts_with(keytree::MEMBER_SEP) {
                    continue;
                }
                tm.entry(w.clone()).or_insert_with(|| inferred.to_string());
            }
        }
        return;
    }
    let (opcode, reads) = inst.flat();
    if opcode.is_empty() {
        return;
    }
    let inferred = infer_op_type(&opcode, &reads, tm);
    if inferred.is_empty() {
        return;
    }
    for w in &inst.writes {
        if w.is_empty() || w.starts_with(keytree::MEMBER_SEP) {
            continue;
        }
        tm.entry(w.clone()).or_insert_with(|| inferred.clone());
    }
}

fn is_cast_op(op: &str) -> bool {
    matches!(
        op,
        "int8"
            | "int16"
            | "int32"
            | "int64"
            | "uint8"
            | "uint16"
            | "uint32"
            | "uint64"
            | "float32"
            | "float64"
    )
}

fn infer_op_type(opcode: &str, reads: &[String], tm: &mut HashMap<String, String>) -> String {
    if matches!(opcode, "return" | "goto" | "br" | "call") {
        return String::new();
    }
    if symbol::lookup(opcode).word == "assign" {
        return if !reads.is_empty() {
            slot_type(&reads[0], tm)
        } else {
            String::new()
        };
    }
    if symbol::lookup(opcode).arith {
        // 字符串拼接走 add：任一读参是 char 族，结果仍是该字符串类型（不是 int64）。
        for r in reads {
            let t = slot_type(r, tm);
            if is_string_kind(&t) {
                return t;
            }
        }
        let word = symbol::lookup(opcode).word.to_string();
        // 单目 sub（一元负号）恒产 int64（runtime EmitInt64）。
        if word == "sub" && reads.len() == 1 {
            return "int64".to_string();
        }
        // 结果 kind 必须与 runtime 一致：同族按更宽者合流（narrow_int/narrow_float）；
        // 任一操作数类型未知（动态 any/空）→ 不声明，runtime 按实际 kind 产出。
        let mut kind = String::new();
        for r in reads {
            let t = slot_type(r, tm);
            if t.is_empty() || !builtin::is_num_kind(&t) {
                return String::new();
            }
            kind = if kind.is_empty() {
                t
            } else {
                builtin::wider_num_kind(&kind, &t)
            };
        }
        if kind.is_empty() {
            return String::new();
        }
        let rk = builtin::op_kind(&word, &kind);
        return if rk.is_empty() { kind } else { rk };
    }
    if symbol::lookup(opcode).cmp {
        return "bool".to_string();
    }
    if opcode == "kvlang·abs" {
        // `&x` 取址产 Ptr：类型记 `*<目标类型>`（目标类型未知则记裸 `*`）——`*` 前缀即 ref 轴标记，
        // 供 `[]` 下标校验拦下「指针当数组」（见 [[ptr]]）。
        let t = reads.first().map(|r| slot_type(r, tm)).unwrap_or_default();
        return format!("*{t}");
    }
    if is_cast_op(opcode) {
        return opcode.to_string();
    }
    if opcode == "obj" {
        return "stringkeymap".to_string();
    }
    if opcode == "map" {
        return "[]stringkeymap".to_string();
    }
    if opcode == "kvspace·set" {
        // 成员写的 base 类型**不在此推断**：map 必须显式声明准确类型
        // （`m:[]char/utf8·int64 = {}`），由 check_map_defined 静态拦截未定义者。
        return String::new();
    }
    match opcode {
        "kvlen" | "ndarray·numel" | "ndarray·dim" | "kvspace·listlen" | "string·len"
        | "string·ord" | "string·cmp" | "string·find" | "string·parseint" | "xv·bodylen" => {
            return "int64".to_string();
        }
        "xv·langtype" => return "[]char/utf8".to_string(),
        "ndarray·shape" => return "[]int64".to_string(),
        "string·char" | "string·set" | "string·slice" | "string·concat" | "string·formatint"
        | "string·formatuint" => return "char/utf32".to_string(),
        "random.int63" => return "int64".to_string(),
        "random·intn" | "random.uint64" | "string·parseuint" => return "uint64".to_string(),
        "pow" | "sqrt" | "exp" | "log" | "string·parsefloat" => return "float64".to_string(),
        "sign" => return "int64".to_string(),
        "abs" | "neg" => {
            return if !reads.is_empty() {
                slot_type(&reads[0], tm)
            } else {
                String::new()
            };
        }
        "max" | "min" => {
            // runtime 取两操作数的更宽数值 kind（narrow_int/narrow_float）。
            let mut kind = String::new();
            for r in reads {
                let t = slot_type(r, tm);
                if t.is_empty() || !builtin::is_num_kind(&t) {
                    return String::new();
                }
                kind = if kind.is_empty() {
                    t
                } else {
                    builtin::wider_num_kind(&kind, &t)
                };
            }
            return kind;
        }
        "xv·at" => {
            if !reads.is_empty() {
                if let Some(t) = tm.get(&format!("{}.0", reads[0])) {
                    return t.clone();
                }
                if let Some(t) = tm.get(&reads[0]) {
                    if let Some(stripped) = t.strip_prefix("[]") {
                        return stripped.to_string();
                    }
                }
            }
            return String::new();
        }
        "xv·set" => {
            if let Some(last) = reads.last() {
                let t = slot_type(last, tm);
                if !t.is_empty() {
                    let key = format!("{}.0", reads[0]);
                    tm.entry(key).or_insert(t);
                }
            }
            return if !reads.is_empty() {
                slot_type(&reads[0], tm)
            } else {
                String::new()
            };
        }
        "xv·reshape" => {
            return if !reads.is_empty() {
                slot_type(&reads[0], tm)
            } else {
                String::new()
            };
        }
        _ => {}
    }
    String::new()
}

/// 表达式操作数的静态类型：字符串字面量按 quote/lit 判定（val 本身不含引号，
/// 不能当变量名查 tm，否则 `"a" + "bc"` 会被同名变量 `a` 带偏）。
fn arg_type(e: &Expr, tm: &HashMap<String, String>) -> String {
    if e.quote != 0 || matches!(e.lit, LitKind::LitString | LitKind::LitRawString) {
        return "char/utf32".to_string();
    }
    match e.lit {
        LitKind::LitInt => "int64".to_string(),
        LitKind::LitFloat => "float64".to_string(),
        LitKind::LitBool => "bool".to_string(),
        _ => slot_type(&e.val, tm),
    }
}

fn slot_type(name: &str, tm: &HashMap<String, String>) -> String {
    if name.is_empty() {
        return String::new();
    }
    if let Some(t) = tm.get(name) {
        return t.clone();
    }
    if name.starts_with('"') {
        return "char/utf32".to_string();
    }
    if name == "true" || name == "false" {
        return "bool".to_string();
    }
    let b = name.as_bytes();
    let digit_start = b[0].is_ascii_digit();
    let neg_start = b[0] == b'-' && b.len() > 1 && b[1].is_ascii_digit();
    if digit_start || neg_start {
        if name.contains('.') || name.contains('e') || name.contains('E') {
            return "float64".to_string();
        }
        return "int64".to_string();
    }
    String::new()
}

fn lit_to_type(lit: LitKind) -> &'static str {
    match lit {
        LitKind::LitInt => "int64",
        LitKind::LitFloat => "float64",
        LitKind::LitString | LitKind::LitRawString => "char/utf32",
        LitKind::LitBool => "bool",
        _ => "",
    }
}

/// 数值算子族的 layout 阶段类型/arity 校验（A1）：已知类型不符即报错，
/// 未知类型（动态 any）保留给运行时。字符串只允许出现在 add（拼接）与比较中。
fn validate_num_op(
    word: &str,
    args: &[Expr],
    tm: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    let is_cmp = matches!(word, "eq" | "neq" | "lt" | "le" | "gt" | "ge");
    let unary = matches!(word, "neg" | "abs" | "sign" | "sqrt" | "exp" | "log");
    if unary && args.len() != 1 {
        push_num_diag(
            diags,
            format!("TypeError: {word} expects 1 operand, got {}", args.len()),
        );
        return;
    }
    if !unary && word == "sub" && !(args.len() == 1 || args.len() == 2) {
        push_num_diag(
            diags,
            format!("TypeError: sub expects 1 or 2 operands, got {}", args.len()),
        );
        return;
    }
    if !unary && !matches!(word, "add" | "mul" | "sub") && args.len() != 2 {
        push_num_diag(
            diags,
            format!("TypeError: {word} expects 2 operands, got {}", args.len()),
        );
        return;
    }
    let mut saw_char = false;
    let mut saw_num = false;
    for a in args {
        let t = arg_type(a, tm);
        if t.is_empty() {
            continue;
        }
        if is_string_kind(&t) {
            saw_char = true;
        } else if builtin::is_num_kind(&t) {
            saw_num = true;
        } else if (t == "bool" || t.starts_with('*')) && is_cmp {
            // bool / Ptr 比较合法（Ptr 对 None 的判空在运行时做值检查）
        } else {
            push_num_diag(
                diags,
                format!("TypeError: {word} expects numeric operand, got {t:?}"),
            );
            return;
        }
    }
    if saw_char && saw_num {
        push_num_diag(
            diags,
            format!("TypeError: {word} cannot mix string and numeric operands"),
        );
    }
}

fn push_num_diag(diags: &mut Vec<Diagnostic>, message: String) {
    diags.push(Diagnostic {
        pos: Pos { line: 0, col: 0 },
        warn: false,
        info: false,
        message,
        source: String::new(),
        src_file: String::new(),
        src_name: String::new(),
    });
}

// ── 特化（对齐 lower/specialize.go） ────────────────────────────────

/// 表达式在「声明 + kind 稳定」视角下的精确 kind；None = 非精确（动态/容器/指针）。
fn exact_kind(
    e: &Expr,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
) -> Option<String> {
    if e.is_leaf() {
        if e.quote != 0 || matches!(e.lit, LitKind::LitString | LitKind::LitRawString) {
            return Some("char/utf32".to_string());
        }
        return match e.lit {
            LitKind::LitInt => Some("int64".to_string()),
            LitKind::LitFloat => Some("float64".to_string()),
            LitKind::LitBool => Some("bool".to_string()),
            LitKind::LitNone if e.val == "None" => None,
            _ => {
                // 变量叶的 lit 也是 LitNone，按名字查声明/稳定表。
                let name = &e.val;
                if name.is_empty() {
                    None
                } else {
                    declared
                        .get(name)
                        .or_else(|| stable.get(name))
                        .filter(|t| !t.is_empty() && !t.contains('|'))
                        .cloned()
                }
            }
        };
    }
    let op = e.op.as_str();
    if is_cast_op(op) {
        return Some(op.to_string());
    }
    if op == "bool" {
        return Some("bool".to_string());
    }
    if op == ast::DEREF_OP || op == "kvlang·abs" {
        return None;
    }
    let mut word = symbol::lookup(op).word.to_string();
    if word.is_empty() {
        word = op.to_string();
    }
    if symbol::lookup(op).cmp {
        return Some("bool".to_string());
    }
    match word.as_str() {
        // 容器读：基座类型声明/稳定且元素 kind 具体 → 结果 kind 精确。
        "xv·at" | "array·compact" | "xv·reshape" => {
            let base = e.args.first()?;
            let t = exact_kind(base, declared, stable)?;
            return container_elem(&t);
        }
        "kvspace·get" => {
            let base = e.args.first()?;
            let t = exact_kind(base, declared, stable)?;
            return container_value(&t);
        }
        "sqrt" | "exp" | "log" | "pow" => return Some("float64".to_string()),
        "sign" => return Some("int64".to_string()),
        "kvlen" | "ndarray·numel" | "ndarray·dim" | "kvspace·listlen" | "string·len"
        | "string·ord" | "string·cmp" | "string·find" | "string·parseint" | "xv·bodylen"
        | "random.int63" => return Some("int64".to_string()),
        "random·intn" | "random.uint64" | "string·parseuint" => {
            return Some("uint64".to_string())
        }
        "string·parsefloat" => return Some("float64".to_string()),
        _ => {}
    }
    let arith = symbol::lookup(op).arith;
    let maxmin = matches!(word.as_str(), "max" | "min");
    if !arith && !maxmin {
        return None;
    }
    if word == "sub" && e.args.len() == 1 {
        // 一元负号恒产 int64（runtime EmitInt64）；操作数 kind 不影响结果 kind。
        return Some("int64".to_string());
    }
    let mut kind = String::new();
    for a in &e.args {
        let t = exact_kind(a, declared, stable)?;
        if !builtin::is_num_kind(&t) {
            return None;
        }
        kind = if kind.is_empty() {
            t
        } else {
            builtin::wider_num_kind(&kind, &t)
        };
    }
    if kind.is_empty() {
        return None;
    }
    let rk = builtin::op_kind(&word, &kind);
    Some(if rk.is_empty() { kind } else { rk })
}

/// kind 稳定推断：只用于**派发资格**（是否发特化 opcode），不是声明类型、不做 coercion。
/// 候选来自推断类型；验证要求该槽的**所有赋值来源**都是同一具体标量 kind：
/// 字面量/显式 cast/声明类型/已稳定变量/结果 kind 精确的内建与算术。
/// `&x`、容器/字段/指针读、调用写参、None、any/union 一律视为不稳定。
pub fn stable_kinds(
    fn_: &Func,
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
) -> HashMap<String, String> {
    let mut sources: HashMap<String, Vec<Expr>> = HashMap::new();
    let mut disq: HashSet<String> = HashSet::new();
    collect_stability(&fn_.body, &mut sources, &mut disq);
    let mut cands: HashMap<String, String> = HashMap::new();
    for (v, es) in &sources {
        if disq.contains(v) || es.is_empty() {
            continue;
        }
        if let Some(t) = tm.get(v) {
            if !t.is_empty() && !t.contains('|') && builtin::is_num_kind(t) {
                cands.insert(v.clone(), t.clone());
            }
        }
    }
    let mut stable: HashMap<String, String> = HashMap::new();
    let mut dead: HashSet<String> = HashSet::new();
    /* 迭代顺序必须与进程哈希种子无关：fixpoint 里 boot/dead 的先后会改变最终稳定集合，
     * 否则同一输入两次 layout 会产出不同的特化集合（881/882/907 波动）。 */
    let mut order: Vec<&String> = cands.keys().collect();
    order.sort_unstable();
    loop {
        let mut changed = false;
        for v in &order {
            let k = &cands[*v];
            if stable.contains_key(*v) || dead.contains(*v) {
                continue;
            }
            let boot = sources
                .get(*v)
                .map(|es| {
                    es.iter()
                        .any(|e| exact_kind(e, declared, &stable).as_deref() == Some(k.as_str()))
                })
                .unwrap_or(false);
            if boot {
                stable.insert((*v).clone(), k.clone());
                changed = true;
            }
        }
        let mut bad: Vec<String> = stable
            .iter()
            .filter(|(v, k)| {
                !sources
                    .get(*v)
                    .map(|es| {
                        es.iter().all(|e| {
                            exact_kind(e, declared, &stable).as_deref() == Some(k.as_str())
                        })
                    })
                    .unwrap_or(false)
            })
            .map(|(v, _)| v.clone())
            .collect();
        bad.sort_unstable();
        for v in bad {
            stable.remove(&v);
            dead.insert(v);
            changed = true;
        }
        if !changed {
            break;
        }
    }
    stable
}

fn collect_inst_stability(
    s: &Instruction,
    sources: &mut HashMap<String, Vec<Expr>>,
    disq: &mut HashSet<String>,
) {
    let e = match &s.expr {
        Some(e) => e,
        None => return,
    };
    for w in &s.writes {
        if slot_is_plain_name(w) {
            sources.entry(w.clone()).or_default().push(e.clone());
        }
    }
    if e.op == "kvlang·abs" {
        if let Some(a) = e.args.first() {
            if a.is_leaf() {
                disq.insert(a.val.clone());
            }
        }
    }
}

fn collect_stability(
    body: &[Stmt],
    sources: &mut HashMap<String, Vec<Expr>>,
    disq: &mut HashSet<String>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => collect_inst_stability(s, sources, disq),
            Stmt::Scope(s) => collect_stability(&s.body, sources, disq),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    collect_inst_stability(c, sources, disq);
                }
                collect_stability(&s.then_, sources, disq);
                collect_stability(&s.else_, sources, disq);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    collect_inst_stability(c, sources, disq);
                }
                collect_stability(&s.body, sources, disq);
            }
            Stmt::For(s) => collect_stability(&s.body, sources, disq),
            _ => {}
        }
    }
}

/// 操作数的派发资格 kind：字面量按自身 kind，变量取声明或稳定推断，其余不算精确。
pub fn exact_operand_kind(
    e: &Expr,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
) -> Option<String> {
    exact_kind(e, declared, stable)
}

/// 写槽名的 exact kind：只接受普通变量名（declared / stable）。
/// 指针解引用、成员/下标、绝对路径等无法静态证明 → None。
pub fn exact_name_kind(
    name: &str,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
) -> Option<String> {
    if name.is_empty()
        || name.starts_with('*')
        || name.starts_with('/')
        || name.contains(keytree::MEMBER_SEP)
        || name.contains('[')
    {
        return None;
    }
    declared
        .get(name)
        .or_else(|| stable.get(name))
        .filter(|t| !t.is_empty() && !t.contains('|'))
        .cloned()
}

/// 特化决策只认**声明类型**或 **kind 稳定**的操作数：其余（推断不稳/无声明/any/union）
/// 在运行时可能是任意 kind，不能进入 `<kind>·<op>`；字面量的 kind 由字面量本身固定。
pub fn specialize(
    fn_: &mut Func,
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    let stable = stable_kinds(fn_, tm, declared);
    specialize_body(&mut fn_.body, tm, declared, &stable, &mut diags);
    diags
}

fn specialize_body(
    body: &mut [Stmt],
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => specialize_inst(s, tm, declared, stable, diags),
            Stmt::Scope(s) => specialize_body(&mut s.body, tm, declared, stable, diags),
            Stmt::If(s) => {
                if let Some(c) = &mut s.cond {
                    specialize_inst(c, tm, declared, stable, diags);
                }
                specialize_body(&mut s.then_, tm, declared, stable, diags);
                specialize_body(&mut s.else_, tm, declared, stable, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &mut s.cond {
                    specialize_inst(c, tm, declared, stable, diags);
                }
                specialize_body(&mut s.body, tm, declared, stable, diags);
            }
            Stmt::For(s) => specialize_body(&mut s.body, tm, declared, stable, diags),
            _ => {}
        }
    }
}

fn specialize_inst(
    inst: &mut Instruction,
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    let e = match &mut inst.expr {
        Some(e) => e,
        None => return,
    };
    if e.is_leaf() {
        return;
    }
    let opcode = e.op.clone();
    let mut word = symbol::lookup(&opcode).word.to_string();
    if word.is_empty() {
        word = opcode.clone();
    }
    if !builtin::num_op(&word) {
        return;
    }
    validate_num_op(&word, &e.args, tm, diags);
    /* 特化决策用 declared（见上）；诊断仍用推断表以保持现有报错面。 */
    /* 只在「全部操作数静态类型完全一致且为 int64/float64」时特化；
     * 混合宽度/混合 int+float/动态 any/字符串/bool 一律保留通用 opcode。 */
    let mut kinds: Vec<String> = Vec::new();
    for arg in &e.args {
        let t = match exact_operand_kind(arg, declared, stable) {
            Some(t) => t,
            None => return,
        };
        if is_string_kind(&t) || !builtin::is_num_kind(&t) {
            return;
        }
        kinds.push(t);
    }
    if kinds.is_empty() {
        return;
    }
    let k = &kinds[0];
    if !(builtin::is_int_kind(k) || k == "float64" || k == "float32") {
        return;
    }
    if kinds.iter().any(|t| t != k) {
        return;
    }
    /* handler 的实参个数必须与之一致（通用 sub 允许 1 参，特化版只收 2 参）。 */
    let is_cmp = matches!(word.as_str(), "eq" | "neq" | "lt" | "le" | "gt" | "ge");
    let unary = matches!(
        word.as_str(),
        "neg" | "abs" | "sign" | "sqrt" | "exp" | "log"
    );
    if e.args.len() != if unary { 1 } else { 2 } {
        return;
    }
    if word == "sign" {
        if k != "int64" {
            return;
        }
    } else if matches!(word.as_str(), "sqrt" | "exp" | "log" | "pow") {
        /* 通用实现对这些恒给 float64；float32 不特化。 */
        if k != "float64" {
            return;
        }
    } else if matches!(word.as_str(), "bitand" | "bitor" | "bitxor" | "shl" | "shr") {
        /* layout 把 bit/shift 的静态结果定义为 64 位；小宽度留待专门修正。 */
        if k != "int64" && k != "uint64" {
            return;
        }
    } else if matches!(word.as_str(), "neg" | "abs") {
        if builtin::is_unsigned_kind(k) {
            return;
        }
    } else if !is_cmp && builtin::op_kind(&word, k) != *k {
        return;
    }
    e.op = format!("{k}{}{word}", keytree::MEMBER_SEP);
}

// ── typed 写槽校验（见 spec [[赋值转换]]）───────────────────────────

/// 结构体表：简单名与 `pkg·名` 都可查。
pub type StructTable = HashMap<String, ast::StructDecl>;

fn struct_lookup<'a>(ty: &str, structs: &'a StructTable) -> Option<&'a ast::StructDecl> {
    let n = ty.strip_prefix("/lib/").unwrap_or(ty);
    structs.get(n)
}

fn slot_is_plain_name(w: &str) -> bool {
    !w.is_empty()
        && !w.starts_with('/')
        && !w.starts_with('*')
        && !w.contains(keytree::MEMBER_SEP)
        && !w.contains('[')
        && !w.contains(']')
}

/// None 字面量：Ident 叶、值为 `None`（普通变量叶的 lit 也是 LitNone，不能按 lit 判）。
fn is_none_lit(e: &Expr) -> bool {
    e.is_leaf() && e.val == "None"
}

/// compact 数组元素 kind：`[dims]K` / `[]K`（含 `[2][3]K`）。
fn container_elem(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    if !t.starts_with('[') {
        return None;
    }
    let last = t.rfind(']')?;
    let e = t[last + 1..].trim();
    if e.is_empty() {
        None
    } else {
        Some(e.to_string())
    }
}

/// map value kind：第一个 `·` 之后。
fn container_value(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    let i = t.find(keytree::MEMBER_SEP)?;
    Some(t[i + keytree::MEMBER_SEP.len()..].to_string())
}

/// 写槽能解析出的声明类型；None = 非 typed 或不可解析。
pub fn resolve_slot_type(
    w: &str,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    declared: &HashMap<String, String>,
) -> Option<String> {
    if slot_is_plain_name(w) {
        return declared.get(w).cloned();
    }
    if let Some(rest) = w.strip_prefix('*') {
        let t = declared.get(rest)?;
        return Some(t.strip_prefix('*').unwrap_or(t).to_string());
    }
    if let Some(open) = w.find('[') {
        let base = &w[..open];
        let t = declared.get(base)?;
        return container_elem(t);
    }
    let mut it = w.split(keytree::MEMBER_SEP);
    let base = it.next()?;
    let mut ty = declared.get(base)?.clone();
    for part in it {
        if let Some(v) = container_value(&ty) {
            ty = v;
        } else if ty.starts_with('[') {
            ty = container_elem(&ty)?;
        } else if let Some(d) = struct_lookup(&ty, structs) {
            ty = d
                .fields
                .iter()
                .find(|f| f.name == part)
                .map(|f| f.ty.clone())?;
        } else {
            return None;
        }
    }
    Some(ty)
}

fn push_write_diag(diags: &mut Vec<Diagnostic>, message: String) {
    diags.push(Diagnostic {
        pos: Pos { line: 0, col: 0 },
        warn: false,
        info: false,
        message,
        source: String::new(),
        src_file: String::new(),
        src_name: String::new(),
    });
}

fn int_bounds(kind: &str) -> Option<(i128, i128)> {
    Some(match kind {
        "int8" => (-128, 127),
        "int16" => (-32768, 32767),
        "int32" => (i32::MIN as i128, i32::MAX as i128),
        "int64" => (i64::MIN as i128, i64::MAX as i128),
        "uint8" => (0, 255),
        "uint16" => (0, 65535),
        "uint32" => (0, u32::MAX as i128),
        "uint64" => (0, u64::MAX as i128),
        _ => return None,
    })
}

/// 字面量对目标标量 kind 的可表示性检查。返回 false = 非数字字面量。
fn check_literal(target: &str, e: &Expr, diags: &mut Vec<Diagnostic>) -> bool {
    let is_int = builtin::is_int_kind(target);
    let is_float = builtin::is_float_kind(target);
    if !is_int && !is_float {
        return false;
    }
    match e.lit {
        LitKind::LitInt => {
            let Ok(v) = e.val.trim().parse::<i128>() else {
                return true;
            };
            if is_int {
                if let Some((lo, hi)) = int_bounds(target) {
                    if v < lo || v > hi {
                        push_write_diag(
                            diags,
                            format!(
                                "TypeError: literal {} is not representable as {target} \
                                 (range {lo}..={hi})",
                                e.val
                            ),
                        );
                    }
                }
            }
            true
        }
        LitKind::LitFloat => {
            let Ok(v) = e.val.trim().parse::<f64>() else {
                return true;
            };
            if is_int {
                if !v.is_finite() || v.fract() != 0.0 {
                    push_write_diag(
                        diags,
                        format!(
                            "TypeError: literal {} is not an integer; cannot assign to {target}",
                            e.val
                        ),
                    );
                    return true;
                }
                if let Some((lo, hi)) = int_bounds(target) {
                    if v < lo as f64 || v > hi as f64 {
                        push_write_diag(
                            diags,
                            format!("TypeError: literal {} is out of range for {target}", e.val),
                        );
                    }
                }
            } else if target == "float32" {
                if !v.is_finite() || v.abs() > f32::MAX as f64 {
                    push_write_diag(
                        diags,
                        format!("TypeError: literal {} is out of float32 range", e.val),
                    );
                }
            } else if !v.is_finite() {
                push_write_diag(diags, format!("TypeError: literal {} is not finite", e.val));
            }
            true
        }
        _ => false,
    }
}

fn check_element(kind: &str, e: &Expr, diags: &mut Vec<Diagnostic>) {
    if !e.is_leaf() {
        return;
    }
    if check_literal(kind, e, diags) {
        return;
    }
    match e.lit {
        LitKind::LitBool => {
            push_write_diag(diags, format!("TypeError: cannot assign bool to {kind}"))
        }
        LitKind::LitString | LitKind::LitRawString => {
            if !is_string_kind(kind) {
                push_write_diag(diags, format!("TypeError: cannot assign string to {kind}"));
            }
        }
        _ => {
            if is_none_lit(e) && !kind.starts_with('*') && !kind.is_empty() {
                push_write_diag(
                    diags,
                    format!("TypeError: None cannot be assigned to {kind}"),
                );
            }
        }
    }
}

fn check_assignable(target: &str, rhs: &str, diags: &mut Vec<Diagnostic>) {
    if rhs.is_empty() {
        return;
    }
    if builtin::is_num_kind(target) {
        if !builtin::is_num_kind(rhs) {
            push_write_diag(diags, format!("TypeError: cannot assign {rhs} to {target}"));
        }
        return;
    }
    if is_string_kind(target) {
        if !is_string_kind(rhs) {
            push_write_diag(diags, format!("TypeError: cannot assign {rhs} to {target}"));
        }
        return;
    }
    if target == "bool" {
        if rhs != "bool" {
            push_write_diag(diags, format!("TypeError: cannot assign {rhs} to bool"));
        }
        return;
    }
    if target.starts_with('*') {
        if !rhs.starts_with('*') && rhs != "nil" {
            push_write_diag(
                diags,
                format!("TypeError: cannot assign {rhs} to pointer {target}"),
            );
        }
        return;
    }
    if target.starts_with('[') || target.contains(keytree::MEMBER_SEP) || target.starts_with('/') {
        if rhs != target {
            push_write_diag(diags, format!("TypeError: cannot assign {rhs} to {target}"));
        }
    }
}

/// 单个值对声明类型的检查：字面量范围优先，其余按静态类型可赋性。
fn check_value(target: &str, e: &Expr, tm: &HashMap<String, String>, diags: &mut Vec<Diagnostic>) {
    if e.is_leaf() {
        if check_literal(target, e, diags) {
            return;
        }
        match e.lit {
            LitKind::LitBool => {
                if target != "bool" {
                    push_write_diag(diags, format!("TypeError: cannot assign bool to {target}"));
                }
                return;
            }
            LitKind::LitString | LitKind::LitRawString => {
                if !is_string_kind(target) {
                    push_write_diag(
                        diags,
                        format!("TypeError: cannot assign string to {target}"),
                    );
                }
                return;
            }
            _ => {
                if is_none_lit(e) {
                    if !target.starts_with('*') && !target.is_empty() {
                        push_write_diag(
                            diags,
                            format!("TypeError: None cannot be assigned to {target}"),
                        );
                    }
                    return;
                }
            }
        }
    }
    check_assignable(target, &expr_result_type(e, tm), diags);
}

fn check_rhs(target: &str, e: &Expr, rhs: &str, diags: &mut Vec<Diagnostic>) {
    if e.is_leaf() {
        if check_literal(target, e, diags) {
            return;
        }
        match e.lit {
            LitKind::LitBool => {
                if target != "bool" {
                    push_write_diag(diags, format!("TypeError: cannot assign bool to {target}"));
                }
                return;
            }
            LitKind::LitString | LitKind::LitRawString => {
                if !is_string_kind(target) {
                    push_write_diag(
                        diags,
                        format!("TypeError: cannot assign string to {target}"),
                    );
                }
                return;
            }
            _ => {
                if is_none_lit(e) {
                    if !target.starts_with('*') && !target.is_empty() {
                        push_write_diag(
                            diags,
                            format!("TypeError: None cannot be assigned to {target}"),
                        );
                    }
                    return;
                }
            }
        }
    }
    check_assignable(target, rhs, diags);
}

fn check_container_literal(
    target: &str,
    e: &Expr,
    structs: &StructTable,
    diags: &mut Vec<Diagnostic>,
) {
    match e.op.as_str() {
        "array" => {
            if let Some(elem) = container_elem(target) {
                if builtin::is_num_kind(&elem) {
                    for a in &e.args {
                        check_element(&elem, a, diags);
                    }
                }
            }
        }
        // 位置型 `{v0,v1,…}`：键为隐式整数坐标，只查值。
        "map" => {
            if let Some(v) = container_value(target) {
                if builtin::is_num_kind(&v) {
                    for a in &e.args {
                        check_element(&v, a, diags);
                    }
                }
            }
        }
        // 命名型 `{k=v}`：键为字符串字面量，值为 map value 或 struct 字段类型。
        "obj" => {
            let mut i = 0;
            while i + 1 < e.args.len() {
                let key = e.args[i].val.trim_matches('"');
                let val = &e.args[i + 1];
                let vt = container_value(target).or_else(|| {
                    struct_lookup(target, structs).and_then(|d| {
                        d.fields
                            .iter()
                            .find(|f| f.name == key)
                            .map(|f| f.ty.clone())
                    })
                });
                if let Some(vt) = vt {
                    if builtin::is_num_kind(&vt) {
                        check_element(&vt, val, diags);
                    }
                }
                i += 2;
            }
        }
        _ => {}
    }
}

fn expr_result_type(e: &Expr, tm: &HashMap<String, String>) -> String {
    if e.is_leaf() {
        return arg_type(e, tm);
    }
    if matches!(e.op.as_str(), "array" | "map" | "obj") {
        return String::new();
    }
    let sym = symbol::lookup(&e.op);
    if sym.arith {
        for a in &e.args {
            let t = arg_type(a, tm);
            if is_string_kind(&t) {
                return t;
            }
        }
        for a in &e.args {
            if arg_type(a, tm) == "float64" {
                return "float64".to_string();
            }
        }
        return "int64".to_string();
    }
    if sym.cmp {
        return "bool".to_string();
    }
    if e.op == "kvlang·abs" {
        let t = e.args.first().map(|a| arg_type(a, tm)).unwrap_or_default();
        return format!("*{t}");
    }
    if is_cast_op(&e.op) {
        return e.op.clone();
    }
    if e.op == "obj" {
        return "stringkeymap".to_string();
    }
    if e.op == "map" {
        return "[]stringkeymap".to_string();
    }
    let mut tm2 = tm.clone();
    let reads: Vec<String> = e.args.iter().map(|a| a.val.clone()).collect();
    infer_op_type(&e.op, &reads, &mut tm2)
}

fn collect_declared(body: &[Stmt], declared: &mut HashMap<String, String>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => {
                for (j, w) in s.writes.iter().enumerate() {
                    if j < s.write_types.len()
                        && !s.write_types[j].is_empty()
                        && slot_is_plain_name(w)
                    {
                        declared
                            .entry(w.clone())
                            .or_insert_with(|| s.write_types[j].clone());
                    }
                }
            }
            Stmt::Scope(s) => collect_declared(&s.body, declared),
            Stmt::If(s) => {
                collect_declared(&s.then_, declared);
                collect_declared(&s.else_, declared);
            }
            Stmt::While(s) => collect_declared(&s.body, declared),
            Stmt::For(s) => collect_declared(&s.body, declared),
            _ => {}
        }
    }
}

fn walk_writes(
    body: &[Stmt],
    pkg: &str,
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    declared: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => walk_inst(s, pkg, sigs, tm, structs, declared, diags),
            Stmt::Scope(s) => walk_writes(&s.body, pkg, sigs, tm, structs, declared, diags),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    walk_inst(c, pkg, sigs, tm, structs, declared, diags);
                }
                walk_writes(&s.then_, pkg, sigs, tm, structs, declared, diags);
                walk_writes(&s.else_, pkg, sigs, tm, structs, declared, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    walk_inst(c, pkg, sigs, tm, structs, declared, diags);
                }
                walk_writes(&s.body, pkg, sigs, tm, structs, declared, diags);
            }
            Stmt::For(s) => walk_writes(&s.body, pkg, sigs, tm, structs, declared, diags),
            _ => {}
        }
    }
}

pub type SigTable = HashMap<String, ast::FuncSig>;

/// struct 字面量 `struct·new(path, field, value, …)`：标量字段的字面量做可表示性检查。
fn check_struct_literal(e: &Expr, structs: &StructTable, diags: &mut Vec<Diagnostic>) {
    let Some(ty_arg) = e.args.first() else { return };
    let ty = ty_arg.val.trim().trim_matches('"');
    let Some(d) = struct_lookup(ty, structs) else {
        return;
    };
    let mut i = 1;
    while i + 1 < e.args.len() {
        let fname = e.args[i].val.trim().trim_matches('"');
        let val = &e.args[i + 1];
        if let Some(f) = d.fields.iter().find(|f| f.name == fname) {
            let ft = f.ty.trim_end_matches("...").trim();
            if builtin::is_num_kind(ft) {
                check_element(ft, val, diags);
            }
        }
        i += 2;
    }
}

/// 调用实参对形参声明类型的检查（typed 参数即 typed 槽，见 [[赋值转换]]）。
fn check_call_args(
    e: &Expr,
    pkg: &str,
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    let sym = symbol::lookup(&e.op);
    if sym.arith || sym.cmp || is_cast_op(&e.op) || matches!(e.op.as_str(), "array" | "map" | "obj")
    {
        return;
    }
    let qual = if pkg.is_empty() {
        e.op.clone()
    } else {
        format!("{pkg}{}{}", keytree::MEMBER_SEP, e.op)
    };
    let Some(sig) = sigs.get(&e.op).or_else(|| sigs.get(&qual)) else {
        return;
    };
    let np = sig.params.len();
    for (i, arg) in e.args.iter().enumerate() {
        let param = if i < np {
            &sig.params[i]
        } else if np > 0 && sig.params[np - 1].ty.ends_with("...") {
            &sig.params[np - 1]
        } else {
            break;
        };
        let raw = param.ty.trim_end_matches("...").trim().to_string();
        if raw.is_empty() || raw.contains('|') || raw.starts_with('@') {
            continue;
        }
        // 只对可静态信任的实参做检查：字面量，或已声明类型的变量。
        // 推断类型可能不准（如 &t·1 的成员取址），不能作为拒绝依据。
        if !arg.is_leaf() {
            continue;
        }
        let is_ptr = raw.starts_with('*');
        let vt = raw.strip_prefix('*').unwrap_or(&raw).to_string();
        if is_none_lit(arg) {
            continue;
        }
        if check_literal(&vt, arg, diags) {
            continue;
        }
        match arg.lit {
            LitKind::LitBool => {
                if vt != "bool" {
                    push_write_diag(diags, format!("TypeError: cannot pass bool to {}", raw));
                }
                continue;
            }
            LitKind::LitString | LitKind::LitRawString => {
                if !is_string_kind(&vt) {
                    push_write_diag(diags, format!("TypeError: cannot pass string to {}", raw));
                }
                continue;
            }
            _ => {}
        }
        if let Some(at) = declared.get(&arg.val) {
            if is_ptr && (at == &raw || at == &format!("*{vt}")) {
                continue;
            }
            check_assignable(&vt, at, diags);
        }
    }
}

fn walk_inst(
    s: &Instruction,
    pkg: &str,
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    declared: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    let Some(e) = &s.expr else { return };
    check_call_args(e, pkg, sigs, tm, declared, diags);
    if e.op == format!("struct{}new", keytree::MEMBER_SEP) {
        check_struct_literal(e, structs, diags);
    }
    // 容器元素写：xv·set(container, index, value) / kvspace·set(container, key, value)。
    if (e.op == "xv·set" || e.op == "kvspace·set") && e.args.len() >= 3 {
        if e.args[0].is_leaf() {
            if let Some(cty) = declared.get(&e.args[0].val) {
                let elem = if e.op == "xv·set" {
                    container_elem(cty)
                } else {
                    container_value(cty)
                };
                if let Some(kind) = elem {
                    if builtin::is_num_kind(&kind) {
                        if let Some(v) = e.args.last() {
                            check_element(&kind, v, diags);
                        }
                    }
                }
            }
        }
    }
    if s.writes.is_empty() {
        return;
    }

    let rhs = expr_result_type(e, tm);
    for (j, w) in s.writes.iter().enumerate() {
        if w.is_empty() {
            return;
        }
        let ann = s.write_types.get(j).map(String::as_str).unwrap_or("");
        let ty = if !ann.is_empty() {
            ann.to_string()
        } else {
            match resolve_slot_type(w, tm, structs, declared) {
                Some(t) => t,
                None => continue,
            }
        };
        let ty = ty.trim().to_string();
        if ty.is_empty() || ty.contains('|') || ty.starts_with('@') {
            return;
        }
        if matches!(e.op.as_str(), "array" | "map" | "obj")
            && (ty.starts_with('[') || ty.contains(keytree::MEMBER_SEP) || ty.starts_with('/'))
        {
            check_container_literal(&ty, e, structs, diags);
        } else {
            check_rhs(&ty, e, &rhs, diags);
        }
    }
}

/// typed 写槽校验（[[赋值转换]]）：可赋性 + 字面量可表示性 + 容器字面量元素。
pub fn validate_writes(
    fn_: &Func,
    pkg: &str,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    sigs: &SigTable,
) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    let mut declared: HashMap<String, String> = HashMap::new();
    for p in fn_.sig.params.iter().chain(fn_.sig.returns.iter()) {
        if !p.ty.is_empty() {
            declared.insert(p.name.clone(), p.ty.clone());
        }
    }
    collect_declared(&fn_.body, &mut declared);
    walk_writes(&fn_.body, pkg, sigs, tm, structs, &declared, &mut diags);
    diags
}
