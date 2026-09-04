//! 控制流 lowering + 类型推断 + 特化（对齐 lower/lower.go、infer.go、specialize.go）。

use std::collections::{HashMap, HashSet};

use super::ast::{self, Expr, Func, Instruction, LitKind, Stmt};
use super::scanner::{Diagnostic, Pos};
use super::{builtin, keytree, symbol};

/// 容器类型（object / stringkeymap / mapexpr）不可用 `[]` 下标访问成员——
/// `[]` 仅限 compact array（shaped kindexpr，含字符串 `[]char/*`）。
fn is_container_type(t: &str) -> bool {
    t == "object" || t == "stringkeymap" || t.contains(keytree::MEMBER_SEP)
}

/// `[]` 下标校验：xv·at/xv·set 基座若为容器类型 → 报错，逼用 kv·get/kv·set/`base·key`。
pub fn check_container_subscript(fn_: &Func) -> Vec<Diagnostic> {
    let tm = infer_types(fn_);
    let mut diags = Vec::new();
    check_subscript_body(&fn_.body, &tm, &mut diags);
    diags
}

fn check_subscript_body(body: &[Stmt], tm: &HashMap<String, String>, diags: &mut Vec<Diagnostic>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => check_subscript_inst(s, tm, diags),
            Stmt::Scope(s) => check_subscript_body(&s.body, tm, diags),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    check_subscript_inst(c, tm, diags);
                }
                check_subscript_body(&s.then_, tm, diags);
                check_subscript_body(&s.else_, tm, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    check_subscript_inst(c, tm, diags);
                }
                check_subscript_body(&s.body, tm, diags);
            }
            Stmt::For(s) => check_subscript_body(&s.body, tm, diags),
            _ => {}
        }
    }
}

fn check_subscript_inst(
    inst: &Instruction,
    tm: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    if let Some(e) = &inst.expr {
        check_subscript_expr(e, tm, diags);
    }
}

fn check_subscript_expr(e: &Expr, tm: &HashMap<String, String>, diags: &mut Vec<Diagnostic>) {
    if (e.op == "xv·at" || e.op == "xv·set") && !e.args.is_empty() {
        let base = &e.args[0];
        if base.is_leaf() {
            if let Some(t) = tm.get(&base.val) {
                if is_container_type(t) {
                    diags.push(Diagnostic {
                        pos: Pos { line: 0, col: 0 },
                        message: format!(
                            "`[]` 下标不能用于容器 `{}`（类型 {}）；容器成员访问用 kv·get/kv·set 或 `{}·key`，`[]` 仅限 compact array",
                            base.val, t, base.val
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
    }
    for a in &e.args {
        check_subscript_expr(a, tm, diags);
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
    Func { comments: Vec::new(), sig: fn_.sig.clone(), body, pkg: String::new() }
}

fn return_inst() -> Stmt {
    Stmt::Instruction(Instruction {
        comments: Vec::new(),
        expr: Some(ast::leaf("return")),
        writes: Vec::new(),
        write_types: Vec::new(),
        arrow_left: false,
        eq: false,
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
                            preamble.extend(expand_sparse(&s.writes[0], e, lg));
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
    let is_obj = s.iter.op == "obj"
        || s.iter.op == "map"
        || (s.iter.is_leaf()
            && matches!(tm.get(&s.iter.val), Some(t) if t == "object" || t == "[]stringkeymap" || t == "stringkeymap"));

    // 迭代源：裸标识符直接原地遍历；表达式（如数组字面量）先物化到临时槽。
    let mut init_body = Vec::new();
    let iter_slot = if s.iter.is_leaf() {
        s.iter.val.clone()
    } else if s.iter.op == "map" {
        let slot = lg.tmp();
        init_body.extend(expand_sparse(&slot, &s.iter, lg));
        slot
    } else {
        let slot = lg.tmp();
        init_body.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(s.iter.clone()),
            writes: vec![slot.clone()],
            write_types: Vec::new(),
            arrow_left: true,
            eq: true,
        }));
        slot
    };
    init_body.push(make_copy_inst("-1", &idx_slot));
    if is_obj {
        init_body.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call("kv·listlen", vec![ast::leaf(&iter_slot)])),
            writes: vec![len_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            eq: false,
        }));
    } else {
        init_body.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call("ndarray·numel", vec![ast::leaf(&iter_slot)])),
            writes: vec![len_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            eq: false,
        }));
    }
    init_body.push(goto_label(&cond_label));

    let add_inst = Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("+", vec![ast::leaf(&idx_slot), ast::leaf("1")])),
        writes: vec![idx_slot.clone()],
        write_types: Vec::new(),
        arrow_left: false,
        eq: false,
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
        eq: false,
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
                "kv·listn",
                vec![ast::leaf(&iter_slot), ast::leaf(&idx_slot)],
            )),
            writes: vec![key_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            eq: false,
        }));
        body_insts.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call(
                "kv·get",
                vec![ast::leaf(&iter_slot), ast::leaf(&key_slot)],
            )),
            writes: vec![s.var.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            eq: false,
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
            eq: false,
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
                eq: false,
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
        eq: false,
    })
}

fn goto_label(label: &str) -> Stmt {
    Stmt::Instruction(Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("goto", vec![ast::leaf(label)])),
        writes: Vec::new(),
        write_types: Vec::new(),
        arrow_left: false,
        eq: false,
    })
}

/// 散 key 数组字面量（parser 产出的 "map"）→ 单条 `map(v0, v1, ...) -> base`，
/// 运行时 map builtin 创建 map 根（kind=map）+ 子键 `base/[i]`（相对 key，方括号索引串）。
fn expand_sparse(base: &str, e: &Expr, _lg: &mut LabelGen) -> Vec<Stmt> {
    let inst = Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("map", e.args.clone())),
        writes: vec![base.to_string()],
        write_types: Vec::new(),
        arrow_left: true,
        eq: false,
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
        eq: false,
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
    // kv.set 成员形（3 读：base, key, val）是 void 无写槽，但 base 仍须推断为 obj/map（供 for-in / 成员访问）
    if e.op == "kv·set" && e.args.len() >= 3 && !e.args[0].val.contains('/') {
        tm.entry(e.args[0].val.clone())
            .or_insert_with(|| "object".to_string());
    }
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
        for r in reads {
            if slot_type(r, tm) == "float64" {
                return "float64".to_string();
            }
        }
        return "int64".to_string();
    }
    if symbol::lookup(opcode).cmp {
        return "bool".to_string();
    }
    if is_cast_op(opcode) {
        return opcode.to_string();
    }
    if opcode == "obj" {
        return "object".to_string();
    }
    if opcode == "map" {
        return "[]stringkeymap".to_string();
    }
    if opcode == "kv·set" {
        // 成员写 base.key = v（3 reads：base, key, value）→ base 是 obj/map。
        if reads.len() >= 3 {
            tm.entry(reads[0].clone())
                .or_insert_with(|| "object".to_string());
        }
        return String::new();
    }
    match opcode {
        "kvlen" | "ndarray·numel" | "ndarray·dim" | "kv·listlen" | "string·len" | "string·ord"
        | "string·cmp" | "string·find" | "string·parseint" | "xv·bodylen" => {
            return "int64".to_string();
        }
        "xv·kindexpr" => return "[]char/utf8".to_string(),
        "ndarray·shape" => return "[]int64".to_string(),
        "kv·list" => return "[]char/utf8".to_string(),
        "string·char" | "string·set" | "string·slice" | "string·concat" | "string·formatint"
        | "string·formatuint" => return "char/utf32".to_string(),
        "random.int63" => return "int64".to_string(),
        "random·intn" | "random.uint64" | "string·parseuint" => return "uint64".to_string(),
        "pow" | "sqrt" | "exp" | "log" => return "float64".to_string(),
        "sign" => return "int64".to_string(),
        "abs" | "neg" | "max" | "min" => {
            return if !reads.is_empty() {
                slot_type(&reads[0], tm)
            } else {
                String::new()
            };
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

// ── 特化（对齐 lower/specialize.go） ────────────────────────────────

pub fn specialize(fn_: &mut Func, tm: &HashMap<String, String>) {
    specialize_body(&mut fn_.body, tm);
}

fn specialize_body(body: &mut [Stmt], tm: &HashMap<String, String>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => specialize_inst(s, tm),
            Stmt::Scope(s) => specialize_body(&mut s.body, tm),
            Stmt::If(s) => {
                if let Some(c) = &mut s.cond {
                    specialize_inst(c, tm);
                }
                specialize_body(&mut s.then_, tm);
                specialize_body(&mut s.else_, tm);
            }
            Stmt::While(s) => {
                if let Some(c) = &mut s.cond {
                    specialize_inst(c, tm);
                }
                specialize_body(&mut s.body, tm);
            }
            Stmt::For(s) => specialize_body(&mut s.body, tm),
            _ => {}
        }
    }
}

fn specialize_inst(inst: &mut Instruction, tm: &HashMap<String, String>) {
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
    let mut kind = String::new();
    for arg in &e.args {
        let t = slot_type(&arg.val, tm);
        if !builtin::is_num_kind(&t) {
            continue;
        }
        if kind.is_empty() {
            kind = t;
        } else {
            kind = builtin::wider_num_kind(&kind, &t);
        }
    }
    if kind.is_empty() {
        return;
    }
    let k = builtin::op_kind(&word, &kind);
    if k.is_empty() {
        return;
    }
    e.op = format!("{k}{}{word}", keytree::MEMBER_SEP);
}
