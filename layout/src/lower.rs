//! 控制流 lowering + 类型推断 + 特化（对齐 lower/lower.go、infer.go、specialize.go）。

use std::collections::{BTreeSet, HashMap, HashSet};

use super::ast::{self, Expr, Func, Instruction, LitKind, MEMBER_READ_OP, Stmt};
use super::scanner::{Diagnostic, Pos};
use super::{builtin, keytree, symbol};

/// 容器类型（stringkeymap / mapexpr）不可用 `[]` 下标访问成员——
/// `[]` 仅限 compact array（shaped langtype，含字符串 `[]char/*`）。
/// 字符串/字符族（含 `[]char/utf32`、`[N]char`）：参与 add 拼接与比较，不做数值特化。
/// `*T` 的**值类型** = `T`。这是「显式解引用」的类型规则，全仓只此一处实现。
///
/// 新参数模型下形参名在声明表里记作 `*T`（`*` 是传递方式），表达式 `*name` 的值类型
/// 就是剥掉一层后的 `T`。推导（`exact_kind`）、实参（`arg_type`）、临时槽
/// （`infer_op_type` 的 assign 分支）、调用返回（`annotate_returns_inst`）四处
/// 必须给出**同一个答案**——否则同一条规则会漂移成四种行为。
pub fn deref_value_type(pointer_type: &str) -> &str {
    pointer_type.strip_prefix('*').unwrap_or(pointer_type)
}

fn is_string_kind(t: &str) -> bool {
    t.contains("char")
}

/// 值类型 `value` 是否匹配声明表达式 `decl`（支持 `a|b|c` 有限 union）。
/// 只用于可证明的实参（字面量 / 已声明变量）；union 值要求每个成员都命中等。
pub fn value_matches(decl: &str, value: &str) -> bool {
    let decl = decl.trim();
    let value = value.trim();
    if decl.is_empty() || value.is_empty() {
        return true;
    }
    value
        .split('|')
        .map(str::trim)
        .filter(|v| !v.is_empty())
        .all(|v| decl.split('|').map(str::trim).any(|a| atom_matches(a, v)))
}

fn split_prefix<'a>(s: &'a str) -> (usize, &'a str) {
    let mut n = 0;
    let mut rest = s;
    while let Some(r) = rest.strip_prefix(['*', '@']) {
        let ch = rest.chars().next().unwrap();
        rest = r;
        if ch == '*' {
            n += 1
        }
    }
    (n, rest)
}

/// value 的类型串拆成 (ndim, dims, kind)：`[2,3]int64` → (2,[2,3],"int64")；
/// `[]char/utf8` → (1,[0],"char/utf8")；`int64` → (0,[],"int64")。
fn value_shape(s: &str) -> (i32, Vec<i32>, String) {
    if let Some(rest) = s.strip_prefix('[') {
        if let Some(end) = rest.find(']') {
            let inner = &rest[..end];
            let dims: Vec<i32> = inner
                .split(',')
                .map(|d| d.trim().parse::<i32>().unwrap_or(0))
                .collect();
            return (dims.len() as i32, dims, rest[end + 1..].to_string());
        }
    }
    (0, Vec::new(), s.to_string())
}

fn atom_matches(a: &str, v: &str) -> bool {
    if a.is_empty() {
        return true;
    }
    let (ap, ar) = split_prefix(a);
    let (vp, vr) = split_prefix(v);
    if ap != vp {
        return false;
    }
    if ar.contains("char") || vr.contains("char") {
        return ar.contains("char") && vr.contains("char");
    }
    if ar == "bool" || vr == "bool" {
        return ar == vr;
    }
    if ar.contains(keytree::MEMBER_SEP) || ar == "stringkeymap" || vr.contains(keytree::MEMBER_SEP)
    {
        return ar == vr;
    }
    if ar.starts_with("/lib/") || vr.starts_with("/lib/") {
        return ar == vr;
    }
    let (ndim, dims, kind) = value_shape(vr);
    crate::langtype::match_langtype(ar, &kind, ndim, &dims)
}

fn is_container_type(t: &str) -> bool {
    t == "stringkeymap" || t.contains(keytree::MEMBER_SEP)
}

/// `[]` 下标校验：xv·at/xv·set 基座若为容器类型 → 报错，逼用 `base·key` 成员读/写。
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
    if e.op != "map·new" {
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
    // 写槽名可以是 `*m`（写穿指针形参）：剥掉 `*` 再按形参名找声明。
    let target_name = target.strip_prefix('*').unwrap_or(target);
    let declared = if ann.is_empty() {
        fn_.sig
            .params
            .iter()
            .chain(fn_.sig.returns.iter())
            .find(|p| &p.name == target_name)
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
    // 基座可以是 `(*p)`：解引用后做成员写，声明的名字在括号里。剥掉括号再查。
    let base_name = base
        .strip_prefix("(*")
        .and_then(|r| r.strip_suffix(')'))
        .unwrap_or(base);
    // 绝对路径容器（/tmp/... 等）全局可见，不需本函数声明；/lib 是结构域。
    if base_name.is_empty() || base_name.starts_with('/') || defined.contains(base_name) {
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
    if (e.op == "compactarray·at" || e.op == "compactarray·set") && !e.args.is_empty() {
        let base = &e.args[0];
        if base.is_leaf() && !addr.contains(&base.val.as_str()) {
            if let Some(t) = tm.get(&base.val) {
                let msg = if is_container_type(t) {
                    Some(format!(
                        "`[]` 下标不能用于容器 `{}`（类型 {}）；容器成员访问用 `{}·key`，`[]` 仅限 compact array",
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
    // First structural error encountered during lowering (e.g. `break` outside loop).
    err: Option<String>,
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

/// call 结果槽带 callee 返回类型：把 rwfunc/rwir 声明的返回类型写进调用指令的
/// `write_types`（声明类型，不是推断类型）。递归/互递归无碍——`sigs` 是文件级
/// 签名表，在各函数体 layout 之前已建好；跨文件调用不在本表内，保持原样。
pub fn annotate_call_returns(fn_: &mut Func, sigs: &SigTable) {
    annotate_returns_body(&mut fn_.body, sigs);
}

fn annotate_returns_body(body: &mut [Stmt], sigs: &SigTable) {
    for st in body {
        match st {
            Stmt::Instruction(s) => annotate_returns_inst(s, sigs),
            Stmt::Scope(s) => annotate_returns_body(&mut s.body, sigs),
            Stmt::If(s) => {
                if let Some(c) = &mut s.cond {
                    annotate_returns_inst(c, sigs);
                }
                annotate_returns_body(&mut s.then_, sigs);
                annotate_returns_body(&mut s.else_, sigs);
            }
            Stmt::While(s) => {
                if let Some(c) = &mut s.cond {
                    annotate_returns_inst(c, sigs);
                }
                annotate_returns_body(&mut s.body, sigs);
            }
            Stmt::For(s) => annotate_returns_body(&mut s.body, sigs),
            _ => {}
        }
    }
}

fn annotate_returns_inst(inst: &mut Instruction, sigs: &SigTable) {
    let e = match &inst.expr {
        Some(e) if !e.is_leaf() => e,
        _ => return,
    };
    let sym = symbol::lookup(&e.op);
    if sym.arith || sym.cmp || is_cast_op(&e.op) {
        return;
    }
    let Some(sig) = sig_lookup(sigs, &e.op) else {
        return;
    };
    for (j, ret) in sig.returns.iter().enumerate() {
        if j >= inst.writes.len() {
            break;
        }
        if inst.write_types.len() <= j {
            inst.write_types.resize(j + 1, String::new());
        }
        // 返回类型上的 `*` 是**传递方式**（写目标必须可引用），不是表达式的类型：
        // 调用 `t·f(*p) -> x` 的结果值就是 `int64`，不是 `*int64`。不剥这一层，
        // 后续 `x + 1` 会报「add expects numeric operand, got "*int64"」。
        //
        // 必须先拼回 `by_ref`：`**N $h` 的 `ret.ty` 只是 `/lib/N`（传递方式单独存），
        // 直接剥它得 `/lib/N`，而正确结果是 `*N`——调用点的 `eh` 于是被判成值。
        let full = ret.pointer_type();
        let value_type = deref_value_type(&full);
        if inst.write_types[j].is_empty() && !value_type.is_empty() {
            inst.write_types[j] = value_type.to_string();
        }
    }
}

/// native rwir 的**名字型**签名（`time`、`duration` 这类不是 TypeSet 位的类型）。
///
/// 唯一源是 `runtimec/specification/native_rwir_decls.kv`——一份**与 rwfunc 同形**的
/// `rwir` 声明片段，由 `generate_rwir_catalog.py` 生成并在 `--check` 里逐份校验；
/// `include_str!` 把它的**文本**编进二进制。
///
/// 用 `.kv` 而不是自定义 X-macro：签名格式**只有一套**（`rwir name(a:T) -> (r:U)`），
/// 解析器也只有一套（`parser::parse_code`）。格式分叉会造出第二份「什么算合法签名」
/// 的判断，那是漂移的源头。
///
/// **构建期同步，运行期不查 kvspace、不查 runtime**——这正是 layout 与 kvspace 的
/// 访问边界（见 `answer-layout-kvspace-access-boundary.md`）。
///
/// 注意：这里**只取内存里的签名表**，绝不调用 `write_rwir_decl`——那会往 `/lib`
/// 写 native rwir 的声明，让每次 layout 都产生一次副作用写。
const NATIVE_RWIR_DECLS: &str =
    include_str!("../../runtimec/specification/native_rwir_decls.kv");

fn native_rwir_signatures() -> &'static HashMap<String, ast::FuncSig> {
    static TABLE: std::sync::OnceLock<HashMap<String, ast::FuncSig>> =
        std::sync::OnceLock::new();
    TABLE.get_or_init(|| {
        /* 表是编译期固定输入：解析不出声明说明生成器漏产，属构建产物损坏，
         * 不是运行期可恢复的状态。与「查表未命中」是两件事——后者保持现状，
         * 前者必须硬失败，否则会静默退化成「没有签名」。 */
        let (file, diags) = crate::parser::parse_code(NATIVE_RWIR_DECLS)
            .unwrap_or_else(|e| panic!("native_rwir_decls.kv: parse failed: {e}"));
        /* `warn`/`info` 之外的都是 error（Diagnostic 没有 level 字段，
         * 用两个布尔位表达三档）。 */
        let errors: Vec<String> = diags
            .iter()
            .filter(|d| !d.warn && !d.info)
            .map(|d| d.string())
            .collect();
        assert!(
            errors.is_empty(),
            "native_rwir_decls.kv: {} error diagnostic(s): {:?}",
            errors.len(),
            errors
        );
        assert!(
            !file.rwir_decls.is_empty(),
            "native_rwir_decls.kv: parsed 0 rwir declarations; the generator produced an empty table"
        );
        file.rwir_decls
            .iter()
            .map(|d| (d.sig.name.clone(), d.sig.clone()))
            .collect()
    })
}

/// 调用名 → 签名：支持裸名、`pkg·名`、`/lib/` 绝对路径。
/// 调用名就是 `/lib` 去掉后的全路径：不做「包内裸名回退」。
///
/// 本文件/已装载代码里查不到时，**兜底查一次** native rwir 生成表：那是
/// `time·now` 这类不落 `/lib` 的能力唯一的名字型签名来源。查不到就返回 None，
/// 由调用方保持现状（裸名、无类型），**不在这里猜**。
pub fn sig_lookup<'a>(sigs: &'a SigTable, op: &str) -> Option<&'a ast::FuncSig> {
    let key = op.strip_prefix("/lib/").unwrap_or(op);
    sigs.get(key)
        .or_else(|| native_rwir_signatures().get(key))
}

/// 将函数体中 if/while 控制流降级为 ScopeStmt + br/goto，展开复合表达式。
/// Parser reports invalid control flow with source positions; this falls back to `Err` without panicking.
pub fn lower_func(fn_: &Func) -> Result<Func, String> {
    let tm = infer_types(fn_);
    let mut lg = LabelGen { n: 0, err: None };
    // 收集已有 br/goto 的跳转目标：这些 label 已可达，lower 时不再补 fall-through goto（幂等）。
    let mut targets = HashSet::new();
    collect_goto_targets(&fn_.body, &mut targets);
    let body = terminate(lower_body(&fn_.body, &mut lg, None, &tm, &targets, true));
    if let Some(e) = lg.err {
        return Err(format!("{}: {e}", fn_.sig.name));
    }
    Ok(Func {
        comments: Vec::new(),
        sig: fn_.sig.clone(),
        body,
        pkg: String::new(),
    })
}

fn return_inst() -> Stmt {
    Stmt::Instruction(Instruction {
        comments: Vec::new(),
        expr: Some(ast::leaf("return")),
        writes: Vec::new(),
        write_types: Vec::new(),
        arrow_left: false,
        operator: false,
        operand_kind: String::new(),
    })
}

/// Append `return` on open paths. Layout panics if a block is still unterminated.
fn terminate(mut body: Vec<Stmt>) -> Vec<Stmt> {
    for st in &mut body {
        if let Stmt::Scope(s) = st {
            /* 只处理**合成**块：它们的入口是跳转进来的，必须跳出去。用户具名块
             * 按线性展平，末尾落下自然衔接到下一条语句——在这里补 `return`
             * 会把后续语句变成不可达（`local/shadowing.kv` 的实测缺陷）。 */
            if s.synthetic {
                s.body = terminate(std::mem::take(&mut s.body));
            }
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

/// `adjacent` indicates that the first statement immediately follows its predecessor in final linear order,
/// allowing an empty preamble to enter a labeled block without a synthetic `goto`. Non-adjacent continuations
/// (after `if`/`while`/`for` or inside split bodies) must emit an explicit `goto`.
fn lower_body(
    stmts: &[Stmt],
    lg: &mut LabelGen,
    lc: Option<&LoopCtx>,
    tm: &HashMap<String, String>,
    targets: &HashSet<String>,
    adjacent: bool,
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
                        if e.op == "map·new" {
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
                let cont = lower_body(&stmts[i + 1..], lg, lc, tm, targets, false);
                return lower_if_with_cont(&preamble, s, cont, lg, lc, tm, targets);
            }
            Stmt::While(s) => {
                let cont = lower_body(&stmts[i + 1..], lg, lc, tm, targets, false);
                return lower_while_with_cont(&preamble, s, cont, lg, lc, tm, targets);
            }
            Stmt::Scope(s) => {
                let mut s = s.clone();
                s.body = lower_body(&s.body, lg, lc, tm, targets, true);
                // Omit fall-through `goto` into a targeted label only when `preamble` is empty and `adjacent` holds.
                let enters_directly =
                    preamble.is_empty() && adjacent && targets.contains(&s.label);
                if !preamble_ends_with_terminator(&preamble) && !enters_directly {
                    preamble.push(goto_label(&s.label));
                }
                let mut out = preamble;
                out.push(Stmt::Scope(s));
                out.extend(lower_body(&stmts[i + 1..], lg, lc, tm, targets, true));
                return out;
            }
            Stmt::Break(_) | Stmt::Continue(_) => {
                let is_break = matches!(st, Stmt::Break(_));
                match lc {
                    Some(lc) => preamble.push(goto_label(if is_break {
                        &lc.break_label
                    } else {
                        &lc.continue_label
                    })),
                    None => {
                        let kw = if is_break { "break" } else { "continue" };
                        lg.err.get_or_insert_with(|| format!("{kw} outside loop"));
                        preamble.push(return_inst());
                    }
                }
                return preamble;
            }
            Stmt::For(s) => {
                let cont = lower_body(&stmts[i + 1..], lg, lc, tm, targets, false);
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

    let then_ = inject_goto(lower_body(&s.then_, lg, lc, tm, targets, false), &merge_label);
    let else_ = inject_goto(lower_body(&s.else_, lg, lc, tm, targets, false), &merge_label);
    let (then_insts, mut then_blocks) = split_insts_and_blocks(then_);
    let (else_insts, mut else_blocks) = split_insts_and_blocks(else_);
    let (cont_insts, cont_blocks) = split_insts_and_blocks(cont);

    inject_goto_blocks(&mut then_blocks, &merge_label);
    inject_goto_blocks(&mut else_blocks, &merge_label);

    let mut result = pre.to_vec();
    result.push(goto_label(&if_label));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
        label: if_label,
        body: cond_body,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
        label: then_label,
        body: then_insts,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
        label: else_label,
        body: else_insts,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
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
        lower_body(&s.body, lg, Some(&body_lc), tm, targets, false),
        &cond_label,
    );
    let (body_insts, mut body_blocks) = split_insts_and_blocks(body_);
    inject_goto_blocks(&mut body_blocks, &cond_label);
    let (cont_insts, cont_blocks) = split_insts_and_blocks(cont);

    let mut result = pre.to_vec();
    result.push(goto_label(&cond_label));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
        label: cond_label,
        body: cond_body,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
        label: body_label,
        body: body_insts,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
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
    // 容器源（mapexpr/structref）用 kvspace·listlen/kvspace·listat + `·*key` 遍历，compact 数组源用
    // ndarray·numel/xv·at（见 spec 控制流脱糖）。判据看**声明的类型**：mapexpr 的 base 恒含
    // `key·value` 的那个 `·`（`[int64]·int64`、`[]char/utf8·int64`），compact 数组不含。
    let is_container = s.iter.op == "map·new"
        || (s.iter.is_leaf() && tm.get(&s.iter.val).is_some_and(|t| t.contains(keytree::MEMBER_SEP)));

    // 迭代源：裸标识符直接原地遍历；表达式（如数组字面量）先物化到临时槽。
    let mut init_body = Vec::new();
    let iter_slot = if s.iter.is_leaf() {
        s.iter.val.clone()
    } else if s.iter.op == "map·new" {
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
            operator: false,
            operand_kind: String::new(),
        }));
        slot
    };
    init_body.push(make_copy_inst("-1", &idx_slot));
    if is_container {
        init_body.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call("kvspace·listlen", vec![ast::leaf(&iter_slot)])),
            writes: vec![len_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            operator: false,
            operand_kind: String::new(),
        }));
    } else {
        init_body.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call("compactarray·numel", vec![ast::leaf(&iter_slot)])),
            writes: vec![len_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            operator: false,
            operand_kind: String::new(),
        }));
    }
    init_body.push(goto_label(&cond_label));

    let add_inst = Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("+", vec![ast::leaf(&idx_slot), ast::leaf("1")])),
        writes: vec![idx_slot.clone()],
        write_types: Vec::new(),
        arrow_left: false,
        operator: false,
        operand_kind: String::new(),
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
        operator: false,
        operand_kind: String::new(),
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
    let body_inner = lower_body(&s.body, lg, Some(&body_lc), tm, targets, false);
    let mut body_insts = Vec::new();
    if is_container {
        body_insts.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call(
                "kvspace·listat",
                vec![ast::leaf(&iter_slot), ast::leaf(&idx_slot)],
            )),
            writes: vec![key_slot.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            operator: false,
            operand_kind: String::new(),
        }));
        body_insts.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call(
                MEMBER_READ_OP,
                vec![ast::leaf(&iter_slot), ast::leaf(&key_slot)],
            )),
            writes: vec![s.var.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            operator: false,
            operand_kind: String::new(),
        }));
    } else {
        body_insts.push(Stmt::Instruction(Instruction {
            comments: Vec::new(),
            expr: Some(ast::call(
                "compactarray·at",
                vec![ast::leaf(&iter_slot), ast::leaf(&idx_slot)],
            )),
            writes: vec![s.var.clone()],
            write_types: Vec::new(),
            arrow_left: false,
            operator: false,
            operand_kind: String::new(),
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
        synthetic: true,
        label: init_label,
        body: init_body,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
        label: cond_label,
        body: cond_body,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
        label: body_label,
        body: body_insts_only,
    }));
    result.push(Stmt::Scope(ast::ScopeStmt {
        comments: Vec::new(),
        synthetic: true,
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
                operator: false,
                operand_kind: String::new(),
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
        operator: false,
        operand_kind: String::new(),
    })
}

fn goto_label(label: &str) -> Stmt {
    Stmt::Instruction(Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("goto", vec![ast::leaf(label)])),
        writes: Vec::new(),
        write_types: Vec::new(),
        arrow_left: false,
        operator: false,
        operand_kind: String::new(),
    })
}

/// 容器字面量的 map langtype 推断（仅用于无写目标可标注的场合，如 for-in 源）：
/// 位置型 `{v0,v1,…}` 键为 1 元坐标 `[int64]`，命名型 `{k=v}` 键为字符串 `[]char/utf8`；
/// 值类型取首个元素的推断类型。推不出（元素类型未知）则空 → runtime 直接 fatal 暴露。
fn infer_sparse_type(e: &Expr, tm: &HashMap<String, String>) -> Vec<String> {
    let (keylt, val) = if e.op == "map·new" {
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

/// 散 key 数组字面量（parser 产出的 "map·new"）→ 单条 `map·new(v0, v1, ...) -> base`，
/// 运行时 map builtin 创建 map 根（kind=map）+ 子键 `base/[i]`（相对 key，方括号索引串）。
fn expand_sparse(base: &str, e: &Expr, wtypes: &[String], _lg: &mut LabelGen) -> Vec<Stmt> {
    let inst = Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("map·new", e.args.clone())),
        writes: vec![base.to_string()],
        // 写类型**必须原样带过来**：容器字面量的 map langtype 就靠它落到写槽
        // （`nums:[int64]·int64 = {…}`；见 [[map容器]]）。曾在此清空，runtime 无从得知容器类型。
        write_types: wtypes.to_vec(),
        arrow_left: true,
        operator: false,
        operand_kind: String::new(),
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
        operator: false,
        operand_kind: String::new(),
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
            d.insert(p.name.clone(), p.value_type());
        }
    }
    collect_declared(&fn_.body, &mut d);
    /*
     * 「首次写入为 None」的槽再补一趟：它们的类型只能从后续写入的唯一具体类型推。
     * 这一处不收集诊断——`declared_types` 是查询接口，没有报错通道；冲突会在
     * `collect_type_sets` 那一趟（有 diags）报出来。
     */
    d
}

pub fn infer_types(fn_: &Func) -> HashMap<String, String> {
    let mut tm = HashMap::new();
    for p in &fn_.sig.params {
        if !p.ty.is_empty() {
            tm.insert(p.name.clone(), p.value_type());
        }
    }
    for p in &fn_.sig.returns {
        if !p.ty.is_empty() {
            tm.insert(p.name.clone(), p.value_type());
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

/// 每条指令各写槽的类型：显式注解（含 call 返回类型）优先，否则统一用
/// 本条表达式的结果类型（字面量 kind / 算子结果）。
fn per_write_types(
    inst: &Instruction,
    tm: &mut HashMap<String, String>,
) -> Vec<Option<String>> {
    let Some(e) = &inst.expr else {
        return Vec::new();
    };
    let shared = if e.is_leaf() && e.lit != LitKind::LitNone {
        lit_to_type(e.lit).to_string()
    } else {
        // 变量拷贝（叶、LitNone）走 flat() 的 `=` 分支，结果类型 = 源变量类型。
        let (opcode, reads) = inst.flat();
        if opcode.is_empty() {
            String::new()
        } else {
            let annotated = inst.write_types.first().map(String::as_str).unwrap_or("");
            infer_op_type(&opcode, &reads, tm, annotated)
        }
    };
    (0..inst.writes.len())
        .map(|j| {
            if let Some(t) = inst.write_types.get(j).filter(|t| !t.is_empty()) {
                return Some(t.clone());
            }
            if shared.is_empty() {
                None
            } else {
                Some(shared.clone())
            }
        })
        .collect()
}

fn infer_inst(inst: &Instruction, tm: &mut HashMap<String, String>) {
    if inst.writes.is_empty() {
        return;
    }
    let types = per_write_types(inst, tm);
    for (w, t) in inst.writes.iter().zip(types) {
        if w.is_empty() || w.starts_with(keytree::MEMBER_SEP) {
            continue;
        }
        if let Some(t) = t {
            tm.entry(w.clone()).or_insert(t);
        }
    }
}

/// 变量的有限类型集合（union 用）：签名/源码声明的类型 + 每条写的类型并集。
/// 用于给操作数槽补类型：单元素 → 具体类型；多元素 → `<t1>|<t2>|…`。
pub fn infer_type_sets(fn_: &Func) -> HashMap<String, Vec<String>> {
    let mut base_sets: HashMap<String, BTreeSet<String>> = HashMap::new();
    let mut base_work: HashMap<String, String> = HashMap::new();
    for p in fn_.sig.params.iter().chain(fn_.sig.returns.iter()) {
        insert_type_set(&mut base_sets, &p.name, &p.ty);
        if !p.name.is_empty() && !p.ty.is_empty() {
            base_work.insert(p.name.clone(), p.ty.clone());
        }
    }
    let mut declared = HashMap::new();
    collect_declared(&fn_.body, &mut declared);
    /*
     * 同上（`infer_type_sets` 也是查询接口，无报错通道）。
     *
     * 冲突的诊断**不在这里报**：这个函数被多处调用，报两次就成了重复诊断。
     * 真正的报错点在 `check_func`/`walk_writes` 那条有 `diags` 的校验链上，
     * 那里会看到「槽没有声明类型」并给出定位。
     */
    for (name, ty) in &declared {
        insert_type_set(&mut base_sets, name, ty);
        if !name.is_empty() && !ty.is_empty() {
            base_work.insert(name.clone(), ty.clone());
        }
    }
    let mut work = base_work;
    let mut sets = base_sets.clone();
    let mut unknown: HashSet<String> = HashSet::new();
    // Fixed-point pass: start each round from `base_sets`, updating `work` in statement order so
    // reads before first write stay unknown/None while multi-type writes propagate their full union.
    for _ in 0..4 {
        let mut next_sets = base_sets.clone();
        let mut next_unknown: HashSet<String> = HashSet::new();
        collect_type_sets(&fn_.body, &mut next_sets, &mut next_unknown, &mut work);
        for (name, set) in &next_sets {
            if !next_unknown.contains(name) || declared.contains_key(name) {
                let joined = join_type_members(set.clone());
                if !joined.is_empty() {
                    work.insert(name.clone(), joined);
                }
            }
        }
        for name in &next_unknown {
            if !declared.contains_key(name) {
                work.remove(name);
            }
        }
        let converged = next_sets == sets && next_unknown == unknown;
        sets = next_sets;
        unknown = next_unknown;
        if converged {
            break;
        }
    }
    // 有任一写无法推断类型的变量：其槽不能静态定类型（运行期实际 kind 可能不同），
    // 只有源码/签名声明过的变量可以例外（声明是语言契约）。
    for name in unknown {
        if !declared.contains_key(&name) {
            sets.remove(&name);
        }
    }
    sets.into_iter()
        .map(|(name, set)| (name, set.into_iter().collect()))
        .collect()
}

fn insert_type_set(
    sets: &mut HashMap<String, BTreeSet<String>>,
    name: &str,
    ty: &str,
) {
    if name.is_empty() || ty.is_empty() {
        return;
    }
    for part in ty.split('|') {
        let part = part.trim();
        if !part.is_empty() {
            sets.entry(name.to_string()).or_default().insert(part.to_string());
        }
    }
}

fn collect_type_sets(
    body: &[Stmt],
    sets: &mut HashMap<String, BTreeSet<String>>,
    unknown: &mut HashSet<String>,
    tm: &mut HashMap<String, String>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => {
                record_inst_sets(s, sets, unknown, tm);
            }
            Stmt::Scope(s) => collect_type_sets(&s.body, sets, unknown, tm),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    record_inst_sets(c, sets, unknown, tm);
                }
                collect_type_sets(&s.then_, sets, unknown, tm);
                collect_type_sets(&s.else_, sets, unknown, tm);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    record_inst_sets(c, sets, unknown, tm);
                }
                collect_type_sets(&s.body, sets, unknown, tm);
            }
            Stmt::For(s) => collect_type_sets(&s.body, sets, unknown, tm),
            _ => {}
        }
    }
}

fn record_inst_sets(
    s: &Instruction,
    sets: &mut HashMap<String, BTreeSet<String>>,
    unknown: &mut HashSet<String>,
    tm: &mut HashMap<String, String>,
) {
    let (op, reads) = s.flat();
    // A read before any write/declaration observes `None` at runtime; mark it unknown so later writes do not back-propagate.
    for r in &reads {
        let base = r.split(keytree::MEMBER_SEP).next().unwrap_or(r);
        if !base.is_empty()
            && !base.starts_with('*')
            && literal_slot_type(base).is_empty()
            && !sets.contains_key(base)
        {
            unknown.insert(base.to_string());
            tm.remove(base);
        }
    }
    // Member reads, `scatterarray·at`, and `kvspace·get` may yield `None` on missing keys; keep unannotated destination slots nullable.
    let nullable_lookup = (symbol::lookup(&op).word == "assign" && reads.len() > 1)
        || matches!(op.as_str(), "scatterarray·at" | "kvspace·get");
    let types = per_write_types(s, tm);
    for (j, w) in s.writes.iter().enumerate() {
        if w.is_empty() || w.starts_with(keytree::MEMBER_SEP) {
            continue;
        }
        let has_explicit_wt = s.write_types.get(j).is_some_and(|t| !t.is_empty());
        if nullable_lookup && !has_explicit_wt {
            unknown.insert(w.clone());
            continue;
        }
        match types.get(j).and_then(|t| t.as_ref()) {
            Some(t) if !t.is_empty() => {
                insert_type_set(sets, w, t);
                if let Some(set) = sets.get(w) {
                    let joined = join_type_members(set.clone());
                    if !joined.is_empty() {
                        tm.insert(w.clone(), joined);
                    }
                }
            }
            _ => {
                unknown.insert(w.clone());
            }
        }
    }
}

/// 有限 union 的成员（`a|b|c` → [a,b,c]）。
pub fn type_members(t: &str) -> Vec<&str> {
    t.split('|').map(str::trim).filter(|s| !s.is_empty()).collect()
}

/// 成员排序后重新拼回 union 串；单成员时就是普通类型。
fn join_type_members(set: BTreeSet<String>) -> String {
    set.into_iter().collect::<Vec<_>>().join("|")
}

/// 字面量操作数自身的类型（数字/字符串/布尔）；非字面量返回空。
pub fn literal_slot_type(name: &str) -> String {
    slot_type(name, &HashMap::new())
}

/// 变量的有限类型集合拼成槽类型：单元素 → 具体类型；多元素 → union。
pub fn type_set_slot_type(sets: &HashMap<String, Vec<String>>, name: &str) -> Option<String> {
    let types = sets.get(name)?;
    let members: BTreeSet<String> = types.iter().cloned().collect();
    if members.is_empty() {
        None
    } else {
        Some(join_type_members(members))
    }
}

/// 文本形式字面量的 kind（与 `slot_type` 同规则）：数字/布尔；其它 None。
fn literal_text_kind(name: &str) -> Option<&'static str> {
    if name == "true" || name == "false" {
        return Some("bool");
    }
    let b = name.as_bytes();
    let digit = b.first().is_some_and(|c| c.is_ascii_digit());
    let neg = b.len() > 1 && b[0] == b'-' && b[1].is_ascii_digit();
    if !digit && !neg {
        return None;
    }
    if name.contains('.') || name.contains('e') || name.contains('E') {
        Some("float64")
    } else {
        Some("int64")
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

/// 算子的结果类型。`annotated` 是写槽上的类型标注（`x:T = …` 的 `T`），容器字面量
/// **有标注用标注、无标注返空**——容器值的 langtype 只有声明处能给出，替它猜一个
/// 就是发明语义（见 spec [[map容器]]：空容器必须带 mapexpr 标注）。
fn infer_op_type(
    opcode: &str,
    reads: &[String],
    tm: &mut HashMap<String, String>,
    annotated: &str,
) -> String {
    if matches!(opcode, "return" | "goto" | "br" | "call") {
        return String::new();
    }
    /* 逻辑算子恒产 bool（catalog 的 result_rule = BOOLEAN，spec 已定）。
     * 这三个算子没有走 arith 分支，此前结果类型为空——`a = not(f(x))` 的写槽
     * 因此没有类型，一路拖到编码器才报「non-concrete langtype」。 */
    if matches!(opcode, "not" | "and" | "or") {
        return "bool".to_string();
    }
    if symbol::lookup(opcode).word == "assign" {
        if reads.len() > 1 {
            // 成员读 `base·seg…`：reads = [base, seg…]；结果 = 容器 value 类型（逐段下钻）。
            let mut ty = slot_type(&reads[0], tm);
            for _ in &reads[1..] {
                if let Some(v) = container_value(&ty) {
                    ty = v;
                } else if ty.starts_with('[') {
                    ty = container_elem(&ty).unwrap_or_default();
                } else {
                    return String::new();
                }
            }
            return ty;
        }
        // 单读参拷贝，两种情形必须分开：
        // - `*a`（显式解引用）：值类型 = 指针指向的类型，剥一层；
        // - `cur`（普通变量拷贝）：值类型就是变量本身的类型，**不剥**。
        //
        // 早先这里无条件 `trim_start_matches('*')` 再剥，于是 `prev = cur`（`cur` 是
        // `*Node`）被推成 `Node`，后续 `prev -> *new_head` 就报「值与指针不符」。
        return if let Some(first) = reads.first() {
            if let Some(inner) = first.strip_prefix('*') {
                deref_value_type(&slot_type(inner, tm)).to_string()
            } else {
                slot_type(first, tm)
            }
        } else {
            String::new()
        };
    }
    if symbol::lookup(opcode).arith || matches!(opcode, "max" | "min" | "abs" | "neg") {
        // 字符串拼接走 add：读参是 char 族（或其 union）时，结果仍是该字符串类型。
        for r in reads {
            let t = slot_type(r, tm);
            let members = type_members(&t);
            if !members.is_empty() && members.iter().all(|m| is_string_kind(m)) {
                return t;
            }
            if is_string_kind(&t) {
                return t;
            }
        }
        let word = symbol::lookup(opcode).word.to_string();
        // 单目 sub（一元负号）恒产 int64（runtime EmitInt64）。
        if word == "sub" && reads.len() == 1 {
            return "int64".to_string();
        }
        // 未定型整数字面量适配（spec 01-种类与定宽类型 第 10 条）：与 specialize 的
        // promoted_numeric_kind 同一套规则，保证写槽类型与发出的 typed opcode 一致。
        {
            let mut ak: Vec<String> = Vec::new();
            let mut al: Vec<Option<&str>> = Vec::new();
            for r in reads {
                let t = slot_type(r, tm);
                let members = type_members(&t);
                if members.is_empty() {
                    return String::new();
                }
                if matches!(literal_text_kind(r), Some("int64")) {
                    ak.push("int64".to_string());
                    al.push(Some(r.as_str()));
                } else {
                    for m in &members {
                        if !builtin::is_num_kind(m) {
                            return String::new();
                        }
                        ak.push(m.to_string());
                        al.push(None);
                    }
                }
            }
            if let Some(target) = adapt_int_uint(&ak, &al) {
                let rk = builtin::op_kind(&word, &target);
                return if rk.is_empty() { target } else { rk };
            }
        }
        // 结果 kind 必须与 runtime 一致：同族按更宽者合流（narrow_int/narrow_float）；
        // 任一操作数类型未知（动态 any/空）→ 不声明，runtime 按实际 kind 产出。
        // union 操作数按成员笛卡尔积求更宽者，结果仍是有限 union。
        let mut combos: BTreeSet<String> = BTreeSet::new();
        for r in reads {
            let t = slot_type(r, tm);
            let members = type_members(&t);
            if members.is_empty() {
                return String::new();
            }
            for m in &members {
                if !builtin::is_num_kind(m) {
                    return String::new();
                }
            }
            if combos.is_empty() {
                combos = members.into_iter().map(str::to_string).collect();
            } else {
                let mut next = BTreeSet::new();
                for a in &combos {
                    for b in &members {
                        next.insert(builtin::wider_num_kind(a, b));
                    }
                }
                combos = next;
            }
        }
        if combos.is_empty() {
            return String::new();
        }
        let mut out = BTreeSet::new();
        for kind in &combos {
            let rk = builtin::op_kind(&word, kind);
            out.insert(if rk.is_empty() { kind.clone() } else { rk });
        }
        return join_type_members(out);
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
    if opcode == "map·new" {
        return annotated.to_string();
    }
    match opcode {
        "kvlen" | "compactarray·numel" | "compactarray·dim" | "kvspace·listlen" | "string·len"
        | "string·ord" | "string·cmp" | "string·find" | "string·parseint" | "compactarray·bodylen"
        | "scatterarray·len" => {
            return "int64".to_string();
        }
        "compactarray·langtype" => return "[]char/utf8".to_string(),
        "compactarray·shape" => return "[]int64".to_string(),
        "string·char" | "string·set" | "string·slice" | "string·concat" | "string·formatint"
        | "string·formatuint" => return "char/utf32".to_string(),
        "random.int63" => return "int64".to_string(),
        "random·intn" | "random.uint64" | "string·parseuint" => return "uint64".to_string(),
        "pow" | "sqrt" | "exp" | "log" | "string·parsefloat" | "math·sin" | "math·cos"
        | "math·tan" | "math·asin" | "math·acos" | "math·atan" | "math·atan2" | "math·sinh"
        | "math·cosh" | "math·tanh" | "math·floor" | "math·ceil" | "math·round" | "math·trunc"
        | "math·log2" | "math·log10" | "math·hypot" => return "float64".to_string(),
        "math·isnan" | "math·isinf" | "math·isfinite" => return "bool".to_string(),
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
        "compactarray·new" => {
            // 字面量构造：元素 kind 全一致时产 `[N]K`（N = 元素个数，与 runtime 定长数组一致）。
            let mut kind = String::new();
            for r in reads {
                let t = slot_type(r, tm);
                if t.is_empty() || !(builtin::is_num_kind(&t) || t == "bool") {
                    return String::new();
                }
                if kind.is_empty() {
                    kind = t;
                } else if kind != t {
                    return String::new();
                }
            }
            if kind.is_empty() {
                return String::new();
            }
            return format!("[{}]{}", reads.len(), kind);
        }
        "compactarray·at" | "scatterarray·at" => {
            if !reads.is_empty() {
                if let Some(t) = tm.get(&format!("{}.0", reads[0])) {
                    return t.clone();
                }
                if let Some(t) = tm.get(&reads[0]) {
                    // compact 数组取元素类型（`[]K` / `[d1,d2]K` 均剥到 K）；
                    // 散 key map 取 value 类型（`[]K·V` → V）。
                    if opcode == "scatterarray·at" {
                        if let Some(v) = container_value(t) {
                            return v;
                        }
                    } else if let Some(e) = container_elem(t) {
                        return e;
                    }
                }
            }
            return String::new();
        }
        "compactarray·set" => {
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
        "compactarray·newreshape" => {
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
        _ => {
            if e.op == ast::MEMBER_READ_OP {
                // 成员读 `base·seg…`：按基座类型逐段取 value 类型。
                // 基座可以是复合表达式（`(*p)·x` 的基座是解引用），不能只查 `val`。
                let mut ty = e
                    .args
                    .first()
                    .map(|a| arg_type(a, tm))
                    .unwrap_or_default();
                for _ in e.args.iter().skip(1) {
                    ty = container_value(&ty).unwrap_or_default();
                }
                return ty;
            }
            // `*p` 解引用：表达式类型是**指针指向的类型**，不是 `*T` 本身。
            // 形参名在 tm 里记作 `*T`（新参数模型的传递方式），剥一层即得值类型。
            if e.op == ast::DEREF_OP {
                let inner = e.args.first().map(|a| slot_type(&a.val, tm)).unwrap_or_default();
                return deref_value_type(&inner).to_string();
            }
            slot_type(&e.val, tm)
        }
    }
}

fn slot_type(name: &str, tm: &HashMap<String, String>) -> String {
    if name.is_empty() {
        return String::new();
    }
    // `*p` 的值类型：查 `p` 的声明再剥一层（`p` 记作 `*T`，`*p` 是 `T`）。
    if let Some(inner) = name.strip_prefix('*') {
        if let Some(t) = tm.get(inner) {
            return deref_value_type(t).to_string();
        }
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
        // 有限 union 按成员判定：全数值 / 全字符串（含只含这两种成员的逻辑或）。
        let members = type_members(&t);
        let all_num = !members.is_empty() && members.iter().all(|m| builtin::is_num_kind(m));
        let all_str = !members.is_empty() && members.iter().all(|m| is_string_kind(m));
        // 可与 None 比较的类型：bool、指针、以及 **key 空间组织**（structref `/lib/X`
        // 与 mapexpr `k·v`）——后者判的是「这个成员目录存在与否」，运行时做值检查。
        let nullable = |m: &str| {
            m == "bool" || m.starts_with('*') || m.starts_with('/') || m.contains('\u{b7}')
        };
        let all_cmp_ok = !members.is_empty() && members.iter().all(|m| nullable(m));
        if is_string_kind(&t) || all_str {
            saw_char = true;
        } else if builtin::is_num_kind(&t) || all_num {
            saw_num = true;
        } else if is_cmp && all_cmp_ok {
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
                // lower 合成的字面量叶（`ast::leaf("1")` / `("-1")`）lit 也是
                // LitNone，但文本本身仍是字面量：按 slot_type 同规则判 kind。
                if let Some(t) = literal_text_kind(&e.val) {
                    return Some(t.to_string());
                }
                // 变量叶的 lit 也是 LitNone，按名字查声明/稳定表。
                let name = &e.val;
                if name.is_empty() {
                    None
                } else {
                    // 声明是并集时不算精确，但仍要回落到稳定推断——否则
                    // `y:int64|float64 = 0; y = y + 1` 永远推不出 int64。
                    declared
                        .get(name)
                        .filter(|t| !t.is_empty() && !t.contains('|'))
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
    if op == ast::DEREF_OP {
        // `*p` 的值类型 = 指针指向的类型。新参数模型下形参名在 declared 里记作 `*T`，
        // 剥掉一层即得。旧实现无条件返回 None，导致 `*a + *b` 推不出字符串、
        // 拼接待糖退化成数值加法。
        let inner = e.args.first().and_then(|a| exact_kind(a, declared, stable))?;
        return Some(deref_value_type(&inner).to_string());
    }
    if op == "kvlang·abs" {
        // `&x` 产 kvlang 指针：结果类型 = `*` + 目标类型（与 `infer_op_type` 同一规则）。
        // 旧实现返回 None，于是 `&x -> eh` 的 `eh` 推不出类型，
        // 再传给 `**T` 形参时判成值与指针不符。
        let inner = e.args.first().and_then(|a| exact_kind(a, declared, stable))?;
        return Some(format!("*{inner}"));
    }
    let entry = symbol::lookup(op);
    let mut word = entry.word.to_string();
    if word.is_empty() {
        word = op.to_string();
    }
    if entry.cmp {
        return Some("bool".to_string());
    }
    if op == ast::MEMBER_READ_OP {
        // 成员读 `base·seg…`（= 读的成员路径）：结果类型 = 容器 value（逐段下钻）。
        // 基座 `base` 声明/稳定为容器时返回其 value 类型；否则未知。
        let base = e.args.first()?;
        let bt = exact_kind(base, declared, stable)?;
        let mut ty = bt;
        for _ in e.args.iter().skip(1) {
            ty = container_value(&ty)?;
        }
        return Some(ty);
    }
    match word.as_str() {
        // 容器读：基座类型声明/稳定且元素 kind 具体 → 结果 kind 精确。
        "compactarray·at" | "scatterarray·at" | "scatterarray·newcompact" | "compactarray·newreshape" => {
            let base = e.args.first()?;
            let t = exact_kind(base, declared, stable)?;
            return container_elem(&t);
        }
        "kvspace·get" => {
            let base = e.args.first()?;
            let t = exact_kind(base, declared, stable)?;
            return container_value(&t);
        }
        "sqrt" | "exp" | "log" | "pow" | "math·sin" | "math·cos" | "math·tan" | "math·asin"
        | "math·acos" | "math·atan" | "math·atan2" | "math·sinh" | "math·cosh" | "math·tanh"
        | "math·floor" | "math·ceil" | "math·round" | "math·trunc" | "math·log2" | "math·log10"
        | "math·hypot" => return Some("float64".to_string()),
        "math·isnan" | "math·isinf" | "math·isfinite" => return Some("bool".to_string()),
        "sign" => return Some("int64".to_string()),
        "kvlen" | "compactarray·numel" | "compactarray·dim" | "kvspace·listlen" | "string·len"
        | "string·ord" | "string·cmp" | "string·find" | "string·parseint" | "compactarray·bodylen"
        | "scatterarray·len" | "random.int63" => return Some("int64".to_string()),
        "random·intn" | "random.uint64" | "string·parseuint" => {
            return Some("uint64".to_string())
        }
        "string·parsefloat" => return Some("float64".to_string()),
        // 字符串族结果：类型精确（char/utf32），供字符串 `+` 的脱糖判定。
        "string·char" | "string·set" | "string·slice" | "string·concat" | "string·formatint"
        | "string·formatuint" => return Some("char/utf32".to_string()),
        _ => {}
    }
    // 只认字形表：exact_kind 的所有调用点都在 specialize 之前（见 analyze_func
    // 的调用顺序），此时 e.op 仍是源码字形，不会是语义名。
    let arith = entry.arith;
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
            // boot 先用**候选表**（cands）而不是已稳定表：互相依赖的循环变量
            // （`rev = r + d` / `r = rev × 10`）需要同一轮里一起点火，否则先被
            // bad 检查误杀。正确性仍由下面的 bad 检查用最终 stable 全量复核。
            let boot = sources
                .get(*v)
                .map(|es| {
                    es.iter()
                        .any(|e| exact_kind(e, declared, &cands).as_deref() == Some(k.as_str()))
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

/// 赋值来源能证明的具体 kind（与 `stable_kinds` 同一套证据，但**不看声明类型**）。
///
/// `stable_kinds` 的候选来自 `infer_types`，而声明为并集的变量在 `infer_types` 里
/// 就是并集，于是永远进不了候选。encoder 需要的正是这种情形：`y:int64|float64 = 1`
/// 的每一次赋值都是 int64，槽上应写 `int64` 而不是并集。
///
/// 证据只有赋值来源：`y = <expr>` 里 `<expr>` 的 exact kind。全部来源一致且为具体
/// 数值 kind 才算数；`&x`、容器/字段/指针读、调用写参、None、union 一律不算。
pub fn assigned_concrete_kinds(
    fn_: &Func,
    declared: &HashMap<String, String>,
) -> HashMap<String, String> {
    let mut sources: HashMap<String, Vec<Expr>> = HashMap::new();
    let mut disq: HashSet<String> = HashSet::new();
    collect_stability(&fn_.body, &mut sources, &mut disq);
    let mut order: Vec<String> = sources
        .iter()
        .filter(|(v, es)| !disq.contains(*v) && !es.is_empty())
        .map(|(v, _)| v.clone())
        .collect();
    order.sort_unstable();
    // boot + 全量复核，与 `stable_kinds` 同一结构：先由**任一**来源点火
    // （`total = 0` 给 int64），再由后续轮次的算术来源接上（`total + i`）。
    let mut found: HashMap<String, String> = HashMap::new();
    let mut dead: HashSet<String> = HashSet::new();
    for _ in 0..4 {
        let mut changed = false;
        for v in &order {
            if found.contains_key(v) || dead.contains(v) {
                continue;
            }
            let Some(es) = sources.get(v) else { continue };
            let boot = es
                .iter()
                .filter_map(|e| exact_kind(e, declared, &found))
                .find(|k| builtin::is_num_kind(k) && !k.contains('|'));
            if let Some(k) = boot {
                found.insert(v.clone(), k);
                changed = true;
            }
        }
        let mut bad: Vec<String> = found
            .iter()
            .filter(|(v, k)| {
                !sources
                    .get(*v)
                    .map(|es| {
                        es.iter().all(|e| {
                            exact_kind(e, declared, &found).as_deref() == Some(k.as_str())
                        })
                    })
                    .unwrap_or(false)
            })
            .map(|(v, _)| v.clone())
            .collect();
        bad.sort_unstable();
        for v in bad {
            found.remove(&v);
            dead.insert(v);
            changed = true;
        }
        if !changed {
            break;
        }
    }
    found
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
    type_sets: &HashMap<String, Vec<String>>,
    stable: &HashMap<String, String>,
) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    specialize_body(&mut fn_.body, tm, declared, stable, type_sets, &mut diags);
    diags
}

/// 变量的推断类型集合全为字符串/字符族（无未知写者）→ 可证明是字符串。
fn sets_all_string(sets: &HashMap<String, Vec<String>>, name: &str) -> bool {
    match sets.get(name) {
        Some(ts) => !ts.is_empty() && ts.iter().all(|t| is_string_kind(t)),
        None => false,
    }
}

fn specialize_body(
    body: &mut [Stmt],
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
    type_sets: &HashMap<String, Vec<String>>,
    diags: &mut Vec<Diagnostic>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => specialize_inst(s, tm, declared, stable, type_sets, diags),
            Stmt::Scope(s) => specialize_body(&mut s.body, tm, declared, stable, type_sets, diags),
            Stmt::If(s) => {
                if let Some(c) = &mut s.cond {
                    specialize_inst(c, tm, declared, stable, type_sets, diags);
                }
                specialize_body(&mut s.then_, tm, declared, stable, type_sets, diags);
                specialize_body(&mut s.else_, tm, declared, stable, type_sets, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &mut s.cond {
                    specialize_inst(c, tm, declared, stable, type_sets, diags);
                }
                specialize_body(&mut s.body, tm, declared, stable, type_sets, diags);
            }
            Stmt::For(s) => specialize_body(&mut s.body, tm, declared, stable, type_sets, diags),
            _ => {}
        }
    }
}

fn specialize_inst(
    inst: &mut Instruction,
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
    type_sets: &HashMap<String, Vec<String>>,
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
    let diags_before = diags.len();
    validate_num_op(&word, &e.args, tm, diags);
    /* 特化决策用 declared（见上）；诊断仍用推断表以保持现有报错面。 */
    /* 操作数类型必须全部可证明（exact_operand_kind）；混合数值 kind 按五语言
     * 提升规则取唯一结果 kind，证明不了（未知/字符串/bool/不可判混合）保留通用 opcode。 */
    let mut kinds: Vec<String> = Vec::new();
    let mut literals: Vec<Option<&str>> = Vec::new();
    let mut string_operands = 0usize;
    for arg in &e.args {
        let t = match exact_operand_kind(arg, declared, stable) {
            Some(t) => t,
            None => {
                // 声明/稳定表都推不出时，退到有限类型集合：全字符串集合可证明是字符串。
                if arg.is_leaf() && sets_all_string(type_sets, &arg.val) {
                    string_operands += 1;
                    kinds.push("char/utf32".to_string());
                    literals.push(None);
                    continue;
                }
                return;
            }
        };
        if is_string_kind(&t) {
            string_operands += 1;
        } else if !builtin::is_num_kind(&t) {
            return;
        }
        literals.push(untyped_int_literal(arg));
        kinds.push(t);
    }
    if kinds.is_empty() {
        return;
    }
    /* 字符串拼接脱糖（spec 05-runtime语义/11）：唯一路径，无开关。
     * 全部操作数可证明是字符串/字符族 → `string·concat`；字符串与非字符串混杂
     * → layout 阶段报错（不留给运行时）；`+` 之外的算子不接受字符串操作数。 */
    if string_operands > 0 {
        if word != "add" {
            // 比较类（`==`/`<`…）在字符串上是合法算子，保持原 opcode 不特化。
            return;
        }
        if string_operands != kinds.len() {
            if diags.len() == diags_before {
                push_num_diag(
                    diags,
                    format!("TypeError: {word} cannot mix string and numeric operands"),
                );
            }
            return;
        }
        e.op = "string·concat".to_string();
        inst.operator = true;
        return;
    }
    let is_cmp = matches!(word.as_str(), "eq" | "neq" | "lt" | "le" | "gt" | "ge");
    /* 混合 int×float 的比较在 runtime 报错（same_class=false）；不特化。
     * k 只用于在 layout 阶段拒掉算子**必定失败**的组合，不再进 opcode。 */
    let mut k = match promoted_numeric_kind(&kinds, !is_cmp, &literals) {
        Some(k) => k,
        None => return,
    };
    // 一元负号（1 参 `-`/sub）：runtime 按 Neg 执行 → 发语义名 `neg`。
    if word == "sub" && e.args.len() == 1 {
        if builtin::is_unsigned_kind(&k) {
            return;
        }
        e.op = "neg".to_string();
        inst.operator = true;
        inst.operand_kind = k;
        return;
    }
    /* handler 的实参个数必须与之一致（通用 sub 允许 1 参，neg 只收 1 参）。 */
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
        /* 通用实现对这些恒给 float64（int 操作数隐式提升）；float32 不特化。 */
        if k == "float32" {
            return;
        }
        k = "float64".to_string();
    } else if matches!(word.as_str(), "bitand" | "bitor" | "bitxor" | "shl" | "shr") {
        /* layout 把 bit/shift 的静态结果定义为 64 位；小宽度留待专门修正。 */
        if k != "int64" && k != "uint64" {
            return;
        }
    } else if matches!(word.as_str(), "neg" | "abs") {
        if builtin::is_unsigned_kind(&k) {
            return;
        }
    } else if !is_cmp && builtin::op_kind(&word, &k) != k {
        return;
    }
    // 只发语义算子词；操作数/结果的精度由槽 langtype 承载，这里记下 promotion
    // 结果供未定型整数字面量落槽。
    e.op = word;
    inst.operator = true;
    inst.operand_kind = k;
}

/// 混合数值 kind 的唯一结果 kind（五语言阵营，且与 runtime 现状一致）：
/// - int×int 取更宽（`wider_int_kind`，C 整型提升）；
/// - float×float 取 float64（有 float64 时）；
/// - int×float 取浮点操作数宽度（C 规则，与顺序无关）：有 float64 → float64，
///   否则 float32；
/// - int×uint 混合 runtime 直接报错（same_class=false），不特化。
/// `allow_mixed_classes=false`（比较类）时跨类混合一律不特化。
/// 未定型整数字面量：叶且整数文本（含 lower 合成的文本叶），返回字面量文本。
/// 浮点字面量不算：浮点走「任一侧浮点则提升为浮点」的既有路径。
fn untyped_int_literal(e: &Expr) -> Option<&str> {
    if !e.is_leaf() || e.quote != 0 {
        return None;
    }
    match e.lit {
        LitKind::LitInt => Some(e.val.as_str()),
        LitKind::LitNone
            if matches!(literal_text_kind(&e.val), Some("int64")) =>
        {
            Some(e.val.as_str())
        }
        _ => None,
    }
}

/// 整数字面量文本是否落在目标整数 kind 的取值范围内。
fn literal_fits(text: &str, kind: &str) -> bool {
    let Ok(v) = text.parse::<i128>() else {
        return false;
    };
    let (signed, width) = match kind {
        "int8" => (true, 8),
        "int16" => (true, 16),
        "int32" => (true, 32),
        "int64" => (true, 64),
        "uint8" => (false, 8),
        "uint16" => (false, 16),
        "uint32" => (false, 32),
        "uint64" => (false, 64),
        _ => return false,
    };
    if signed {
        let max = (1i128 << (width - 1)) - 1;
        v >= -max - 1 && v <= max
    } else {
        v >= 0 && (v as u128) <= (1u128 << width) - 1
    }
}

/// int×uint 混合时，若冲突只来自「未定型整数字面量」，把字面量适配到确定类型那一侧。
/// 规则（spec 01-种类与定宽类型 第 10 条）：字面量按上下文适配；只有两侧都是**确定**
/// 且冲突的符号性才保持混合冲突（返回 None）。
fn adapt_int_uint(kinds: &[String], literals: &[Option<&str>]) -> Option<String> {
    let (mut det_int, mut det_uint) = (None::<String>, None::<String>);
    let mut has_lit = false;
    for (i, k) in kinds.iter().enumerate() {
        if literals.get(i).is_some_and(|l| l.is_some()) {
            has_lit = true;
            continue;
        }
        if builtin::is_unsigned_kind(k) {
            det_uint = Some(match det_uint {
                Some(t) => builtin::wider_int_kind(&t, k),
                None => k.clone(),
            });
        } else if builtin::is_int_kind(k) {
            det_int = Some(match det_int {
                Some(t) => builtin::wider_int_kind(&t, k),
                None => k.clone(),
            });
        } else {
            return None;
        }
    }
    if !has_lit {
        return None;
    }
    let target = match (det_int, det_uint) {
        (Some(_), Some(_)) => return None, // 真冲突：两侧都是确定类型
        (Some(t), None) | (None, Some(t)) => t,
        (None, None) => return None,
    };
    for (i, lit) in literals.iter().enumerate() {
        if let Some(text) = lit {
            if !literal_fits(text, &target) {
                return None;
            }
        }
        if kinds.get(i).is_some_and(|k| builtin::is_float_kind(k)) {
            return None;
        }
    }
    Some(target)
}

fn promoted_numeric_kind(
    kinds: &[String],
    allow_mixed_classes: bool,
    literals: &[Option<&str>],
) -> Option<String> {
    let (mut has_int, mut has_uint, mut has_float) = (false, false, false);
    let mut has_float64 = false;
    for k in kinds {
        match k.as_str() {
            "int8" | "int16" | "int32" | "int64" => has_int = true,
            "uint8" | "uint16" | "uint32" | "uint64" => has_uint = true,
            "float32" => has_float = true,
            "float64" => {
                has_float = true;
                has_float64 = true;
            }
            _ => return None,
        }
    }
    if has_int && has_uint {
        // 混合符号：字面量在场且能适配到确定类型一侧时不算冲突；浮点在场时保持原行为
        // （不在此特化，留给浮点提升/运行时判别，避免顺手改动浮点路径）。
        if has_float {
            return None;
        }
        return adapt_int_uint(kinds, literals);
    }
    let mixed_class = has_float && (has_int || has_uint);
    if mixed_class {
        if !allow_mixed_classes {
            return None;
        }
        // C 规则（spec §数值 promotion）：结果宽度只由浮点操作数决定，整数
        // 转成该宽度；与操作数顺序无关。旧实现用位/字节混比 wmax，是 bug。
        return Some(if has_float64 { "float64" } else { "float32" }.to_string());
    }
    if has_float {
        return Some(if has_float64 { "float64" } else { "float32" }.to_string());
    }
    let mut k = kinds[0].clone();
    for t in &kinds[1..] {
        k = builtin::wider_num_kind(&k, t);
    }
    Some(k)
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
        && !w.starts_with("(*")
        /*
         * `(*p)` 是**解引用后取成员**的形态（`(*p)·x` 的基座），不是普通名。
         *
         * 少了这一条时它被判成普通名：既不以 `/`、`*` 开头，也不含 `·`/`[`/`]`。
         * 后果是 `resolve_slot_type` 里 `(*p)` 那个分支**永远不可达**——那条分支
         * 存在但从不执行，等于没有。
         *
         * 与 `(x)` 这类括号表达式区分：只有 `(*` 开头才是解引用形态。
         */
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
pub fn container_value(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    let i = t.find(keytree::MEMBER_SEP)?;
    Some(t[i + keytree::MEMBER_SEP.len()..].to_string())
}

/// 调用结果类型：**返回类型是实参的函数**的那些算子。
///
/// 判据（架构师）：**它的返回类型能否在完全不看实参的情况下写出来？**
/// 不能的，进不了 `native_rwir_decls.kv`（那张表是「无前因的常量类型」专用），
/// 只能由 layout 从实参推：
///   - 容器键类型：`kvspace·listat`（`[T]·V` → `T`，`[]X·V` → `[]X`）
///   - 形态变换 / 取元素 / 写回同容器：后续批次加，见 workspace/622/coding1 报告
///
/// 规则集中在这张表里，**不散进 `expr_slot_type` 或 `infer_op_type`**——散开之后
/// 将来要把它们迁进 catalog 就得满文件找。
///
/// 任一实参类型不可知 → 返回 `None`（失败封闭，编码器照旧拒绝）。**不许猜**，
/// 更不许用 `any` 之类的占位类型：那不是「先不管类型」，是「放弃类型检查」。
///
/// **死法（必须写明）：** 这些规则最终应迁入 catalog 的 `result_rule`（数据驱动、
/// 单一来源）。迁移时**删除本函数**，不是「在这里补一层兼容」——两处并存就是两份
/// 真相，而两份真相必然漂移。当前延期理由见
/// `workspace/622/待裁决-2026-11-04.md` 的「catalog result_rule 词汇表扩展」。
pub fn call_result_type(
    operation: &str,
    argument_types: &[Option<String>],
    argument_literals: &[Option<String>],
) -> Option<String> {
    match operation {
        // 容器键类型。
        "kvspace·listat" => container_key_type(argument_types.first()?.as_deref()?),
        // 形态变换：compact `[]T` ↔ 散列 `[int64]·T`（spec 11：散列数组的容器 langtype
        // 是完整 map 类型，键为一元元组 `[int64]`）。
        "scatterarray·newscatter" => compact_to_scatter(argument_types.first()?.as_deref()?),
        "scatterarray·newcompact" => scatter_to_compact(argument_types.first()?.as_deref()?),
        // 取子集：切片结果的类型与源相同（都是 compact）。
        "compactarray·newslice" => compact_argument_type(argument_types.first()?.as_deref()?),
        // 取元素。
        "compactarray·at" => container_elem(argument_types.first()?.as_deref()?),
        // 目标类型**来自字面量**（不是推导）：这一类与其余四类的区别是「读出来」而不是
        // 「推出来」。读出来的东西也要过判据——否则它就成「用户写什么类型就是什么类型」
        // 的后门。运行时确实会再校验一次（`container_replace_langtype` 取该串做新
        // kindexpr，`container_langtype_text` 不认就失败），但 layout 必须更早拦住。
        "compactarray·newreinterpret" => reinterpret_target_type(
            argument_literals.get(1).and_then(|literal| literal.as_deref()),
        ),
        _ => None,
    }
}

/// `[]T`（compact）→ `[int64]·T`（散列）。已是散列形态 → None（无隐式跨形态）。
fn compact_to_scatter(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    if t.contains(keytree::MEMBER_SEP) || !t.starts_with('[') {
        return None;
    }
    let element = container_elem(t)?;
    Some(format!("[int64]{}{element}", keytree::MEMBER_SEP))
}

/// `[K]·V`（散列）→ `[]V`（compact）。
fn scatter_to_compact(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    if !t.contains(keytree::MEMBER_SEP) {
        return None;
    }
    let value = container_value(t)?;
    if value.is_empty() {
        return None;
    }
    Some(format!("[]{value}"))
}

/// 切片源必须是 compact（`[]T`）；结果类型与源相同。
fn compact_argument_type(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    if t.contains(keytree::MEMBER_SEP) || !t.starts_with('[') {
        return None;
    }
    Some(t.to_string())
}

/// `newreinterpret` 的目标类型：字面量文本，**必须过 langtype 判据**（非空、非并集、语法合法）。
/// 判据复用 `crate::langtype`，不在这里另写一份「像不像 langtype」。
fn reinterpret_target_type(literal: Option<&str>) -> Option<String> {
    let text = literal?.trim();
    if text.is_empty() || crate::langtype::has_union(text) || !crate::langtype::valid_langtype(text)
    {
        return None;
    }
    Some(text.to_string())
}

/// 容器的**键值类型**：键是普通值，与 [`container_value`]（`·` 之后的值类型）不同名不同义。
///
/// 按 spec `03-类型系统/14-map容器.kv`：
///   - `[T]·V`   一元元组键  → `T`（例：`[int64]·int64` 的键是 int64）
///   - `[]X·V`   字符串键    → 整个 `[]X`（例：`[]char/utf32·int64` 的键是字符串）
///   - `T·V`     裸标量键    → `T`
///   - `[T1,T2]·V`（多元组键）→ **None**：listat 返回的是一段坐标，没有单一值类型可证
///   - `/lib/X`（structref）→ **None**：struct 遍历在 lowering 里走
///     `compactarray·numel` + `compactarray·at`，**根本不产生 `listat`**；
///     这条是**缺口不是设计**——给 struct 编一个键类型连用例都没有，属于发明语义。
pub fn container_key_type(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    let i = t.find(keytree::MEMBER_SEP)?;
    let key = &t[..i];
    if key.starts_with("/lib/") {
        return None;
    }
    let Some(inner) = key.strip_prefix('[').and_then(|rest| rest.strip_suffix(']')) else {
        // 裸标量键（`int32·int32`）：键类型就是它自己。
        return if key.is_empty() { None } else { Some(key.to_string()) };
    };
    // `[]X` 是字符串/数组类型（键本身就是数组），原样返回；
    // `[T]` 是一元元组键，取里面那个类型。
    if inner.starts_with(']') {
        return Some(key.to_string());
    }
    if inner.contains(',') || inner.contains(']') {
        return None;
    }
    Some(inner.to_string())
}

/// 写槽能解析出的声明类型；None = 非 typed 或不可解析。
pub fn resolve_slot_type(
    w: &str,
    _tm: &HashMap<String, String>,
    structs: &StructTable,
    declared: &HashMap<String, String>,
) -> Option<String> {
    if slot_is_plain_name(w) {
        return declared.get(w).cloned();
    }
    // `(*p)·seg…`：先解引用得值的类型，再逐段下钻成员。
    // 写成 `(*p)` 是因为 `*T` 的成员访问必须显式解引用（spec 16-ptr），
    // 不加括号会读成 `*(p·seg)`。基座解引用后是**值**，`·` 才是成员。
    if let Some(rest) = w.strip_prefix("(*") {
        if let Some((base, tail)) = rest.split_once(keytree::MEMBER_SEP) {
            let inner = base.strip_suffix(')')?;
            let t = declared.get(inner)?;
            return Some(member_chain_type(deref_value_type(t), tail, structs)?);
        }
        // 纯解引用 `(*p)`（无成员段）。
        let inner = rest.strip_suffix(')')?;
        let t = declared.get(inner)?;
        return Some(deref_value_type(t).to_string());
    }
    if let Some(rest) = w.strip_prefix('*') {
        let t = declared.get(rest)?;
        return Some(deref_value_type(t).to_string());
    }
    if let Some(open) = w.find('[') {
        let base = &w[..open];
        let t = declared.get(base)?;
        return container_elem(t);
    }
    let mut it = w.split(keytree::MEMBER_SEP);
    let base = it.next()?;
    let ty = declared.get(base)?.clone();
    let tail: Vec<&str> = it.collect();
    member_chain_type(&ty, &tail.join(&keytree::MEMBER_SEP.to_string()), structs)
}

/// 沿 `·` 逐段取成员类型：mapexpr 取 value、数组取元素、struct 取字段声明。
pub(crate) fn member_chain_type(start: &str, tail: &str, structs: &StructTable) -> Option<String> {
    let mut ty = start.to_string();
    if tail.is_empty() {
        return Some(ty);
    }
    for part in tail.split(keytree::MEMBER_SEP) {
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

/// Validate struct field default `f:T=...` (must be a representable scalar literal, matching local decl rules).
pub fn check_field_default(name: &str, ty: &str, e: &Expr) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    if ty.starts_with('*') || ty.starts_with('@') {
        return diags; // Pointer/ext fields default to None.
    }
    let (dims, base) = super::kvkind::parse_langtype(ty);
    let scalar = dims.is_empty()
        && (builtin::is_int_kind(&base) || builtin::is_float_kind(&base) || base == "bool");
    if !scalar {
        return diags; // Non-numeric kinds (char/*, arrays, structref) are not scalar-encoded here.
    }
    if !e.is_leaf() {
        push_write_diag(
            &mut diags,
            format!("struct field {name:?}: default for {ty} must be a literal, got `{e}`"),
        );
    } else if base == "bool" {
        if e.quote != 0 || (e.val != "true" && e.val != "false") {
            push_write_diag(
                &mut diags,
                format!("struct field {name:?}: bool default must be true/false, got {:?}", e.val),
            );
        }
    } else if !check_literal(&base, e, &mut diags) {
        push_write_diag(
            &mut diags,
            format!("struct field {name:?}: default for {ty} must be a numeric literal, got {:?}", e.val),
        );
    }
    diags
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
        // **指针与值不同型**：`p`（`*T`）得地址值、`*p` 得值（spec 16-ptr）。
        // 不能剥掉 `*` 再比——那会让「值写进指针槽」也通过，类型检查形同虚设。
        if rhs != target {
            push_write_diag(diags, format!("TypeError: cannot assign {rhs} to {target}"));
        }
    }
}

fn check_rhs(
    target: &str,
    e: &Expr,
    rhs: &str,
    nullable: bool,
    diags: &mut Vec<Diagnostic>,
) {
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
                    // `None` 是**唯一**的空指针字面量（spec 16-ptr「空指针即 None」），
                    // 凡声明为 `*T` 的槽都接受它。但 `target` 已被剥掉 `*`（那是传递
                    // 方式，不是类型的一部分），所以不能靠 `target.starts_with('*')`
                    // 判断——由调用方按**声明**给出 `nullable`。
                    if !nullable && !target.is_empty() {
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

fn check_container_literal(target: &str, e: &Expr, diags: &mut Vec<Diagnostic>) {
    match e.op.as_str() {
        "compactarray·new" => {
            if let Some(elem) = container_elem(target) {
                if builtin::is_num_kind(&elem) {
                    for a in &e.args {
                        check_element(&elem, a, diags);
                    }
                }
            }
        }
        // 位置型 `{v0,v1,…}`：键为隐式整数坐标，只查值。
        // 键值组构造，两种成员写法共用这一个算子：
        // - 位置型 `{v0, v1, …}`：每个 arg 都是值，逐个检查；
        // - 具名型 `{k=v, …}`：args 是 `键(str_lit), 值` 交错，只检查值（跳过奇数位）。
        // 判据是「首参是字符串字面量」——具名的键由 parser 包成 `str_lit`。
        "map·new" => {
            let Some(v) = container_value(target) else {
                return;
            };
            if !builtin::is_num_kind(&v) {
                return;
            }
            let named = e.args.first().is_some_and(|a| a.quote != 0);
            let step = if named { 2 } else { 1 };
            let offset = usize::from(named);
            for a in e.args.iter().skip(offset).step_by(step) {
                check_element(&v, a, diags);
            }
        }
        _ => {}
    }
}

fn expr_result_type(e: &Expr, tm: &HashMap<String, String>, annotated: &str) -> String {
    if e.is_leaf() {
        return arg_type(e, tm);
    }
    // `*p` 的结果类型 = 指针指向的值类型（`p` 记作 `*T`，剥一层得 `T`）。
    // 这一支缺失时 `*root` 的 rhs 为空，`check_assignable` 因 rhs 空直接放行，
    // 「值写进指针槽」就漏过去了。
    if e.op == ast::DEREF_OP {
        let inner = e
            .args
            .first()
            .map(|a| arg_type(a, tm))
            .unwrap_or_default();
        return deref_value_type(&inner).to_string();
    }
    if matches!(e.op.as_str(), "compactarray·new" | "map·new") {
        return annotated.to_string();
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
    let mut tm2 = tm.clone();
    let reads: Vec<String> = e.args.iter().map(|a| a.val.clone()).collect();
    infer_op_type(&e.op, &reads, &mut tm2, annotated)
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
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    declared: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => walk_inst(s, sigs, tm, structs, declared, diags),
            Stmt::Scope(s) => walk_writes(&s.body, sigs, tm, structs, declared, diags),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    walk_inst(c, sigs, tm, structs, declared, diags);
                }
                walk_writes(&s.then_, sigs, tm, structs, declared, diags);
                walk_writes(&s.else_, sigs, tm, structs, declared, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    walk_inst(c, sigs, tm, structs, declared, diags);
                }
                walk_writes(&s.body, sigs, tm, structs, declared, diags);
            }
            Stmt::For(s) => walk_writes(&s.body, sigs, tm, structs, declared, diags),
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
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    let sym = symbol::lookup(&e.op);
    if sym.arith || sym.cmp || is_cast_op(&e.op)
        || matches!(e.op.as_str(), "compactarray·new" | "map·new")
    {
        return;
    }
    let Some(sig) = sig_lookup(sigs, &e.op) else {
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
        // 用 `pointer_type()` 拼回 `*`/`@` 前缀：`**Node $head` 的 `param.ty` 只有
        // `Node`，传递方式单独存在 `by_ref` 里。只读 `ty` 会让实参要求少一层指针。
        // `...` 是签名层变参标记，仍在拼回后剥掉。
        let raw = param
            .pointer_type()
            .trim_end_matches("...")
            .trim()
            .to_string();
        if raw.is_empty() || raw.starts_with('@') {
            continue;
        }
        // `*` 作用于**整个类型表达式**（spec [[ptr]]：`*` 是传递方式，不是类型的
        // 一部分）。`*int64|float64` 读作「指向 (int64|float64) 的指针」，比对的是
        // 剥掉 `*` 之后的声明类型；逐成员写 `*A|*B` 非法，由参数声明处拒绝。
        let declared_value_type = deref_value_type(&raw).to_string();
        // **形状与并集判定不在这里**：它在 `check_call_shapes`（lower 之前的 AST 上），
        // 因为 `flatten_expr` 到这一层已经把 `f([1,2,3,4])` 的实参物化成临时槽，
        // 容器字面量不复存在。这里只做**变量实参**的具体类型检查——那是
        // `check_call_shapes` 拿不到 `declared` 而做不了的那部分。
        //
        // 写这段时踩过两次：①只按「含不含 `|`」分流，非并集从不走形状匹配；
        // ②把形状检查也放这里，于是同一错误在两处各报一条（并集实参重复报）。
        // 判据的归属要按**能看见什么**分：AST 层看得见字面量，写槽层看得见变量。
        if is_matchable_kindexpr(&declared_value_type) {
            if let Some(vt) = provable_arg_type(arg, declared, tm) {
                // **归属按能看见什么分**：
                //  · 字面量（含容器字面量）——`check_call_shapes` 在 AST 层已判过，
                //    这里跳过，否则同一错误报两条（并集坏实参重复报，实测过）；
                //  · 变量——只有这里看得见 `declared`，必须在这里按**同一条**
                //    `value_matches` 判。否则会落到下面 `check_assignable` 的字符串
                //    相等，把 `[?]int64` 收到 `[3]int64` 判成不等（量词全被误伤，实测过）。
                if is_variable_arg(arg) && !value_matches(&declared_value_type, &vt) {
                    push_write_diag(
                        diags,
                        format!(
                            "TypeError: cannot pass {vt} to {declared_value_type} (parameter {})",
                            param.name
                        ),
                    );
                }
                continue;
            }
        }
        // 只对可静态信任的实参做检查：字面量，或已声明类型的变量。
        // 推断类型可能不准（如 &t·1 的成员取址），不能作为拒绝依据。
        if !arg.is_leaf() {
            continue;
        }
        let is_ptr = raw.starts_with('*');
        let vt = deref_value_type(&raw).to_string();
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

/// 该实参是不是**变量引用**（裸名且不是字面量文本）。
///
/// 用来区分两种实参的归属：字面量在 AST 层由 `check_call_shapes` 判，变量在此处判。
/// 两者共用 `value_matches`，只是**看得见的东西不同**——不是两套判据。
///
/// 不能靠 `e.lit` 判断：`flatten_expr` 把字面量统一重建为 `ast::leaf`，那里
/// `lit` 恒为 `LitNone`（`ast.rs:315`），类型信息在**文本**里（`"true"`、`"42"`）。
/// 判据只能与 `literal_slot_type` 同源，否则 `true` 会被当成变量（实测过：
/// 并集坏实参因此报两条）。
fn is_variable_arg(e: &Expr) -> bool {
    e.is_leaf()
        && e.quote == 0
        && !e.val.is_empty()
        && literal_slot_type(&e.val).is_empty()
}

/// 该声明类型能不能用 `value_matches` 匹配（即它是不是一条 kindexpr）。
///
/// 排除两类：structref（`/lib/…`）与 mapexpr（含 `·`）——它们的相等性由
/// `atom_matches` 的字符串比对承担，进 `match_langtype` 只会得到一个假确定。
/// 空串（空 kindexpr，不约束）返回 false：那种形参不该被调用点拦。
fn is_matchable_kindexpr(ty: &str) -> bool {
    !ty.is_empty()
        && !ty.starts_with("/lib/")
        && !ty.contains(keytree::MEMBER_SEP)
        && ty != "stringkeymap"
}

/// 容器字面量的类型：`[a, b, c]` → `[3]K`，K 是元素的一致 kind。
///
/// **只推导 compact 数组字面量**（`compactarray·new`）。散 key `{…}` 与 struct 字面量
/// 返回 None：它们的类型取决于写目标（`{}` 是 struct 还是 map 由声明决定，见
/// spec 03-类型系统/17-struct），从字面量本身推不出来——硬推会造出第二个事实源。
///
/// 与 `derive_kind` 的 `compactarray·new` 分支同一规则：元素 kind 全一致才产 `[N]K`。
fn container_literal_type(e: &Expr, tm: &HashMap<String, String>) -> Option<String> {
    if e.op != "compactarray·new" || e.args.is_empty() {
        return None;
    }
    let mut kind = String::new();
    for element in &e.args {
        let t = provable_arg_type(element, &HashMap::new(), tm)?;
        let t = t.trim().to_string();
        if !builtin::is_num_kind(&t) && t != "bool" {
            return None;
        }
        if kind.is_empty() {
            kind = t;
        } else if kind != t {
            return None;
        }
    }
    Some(format!("[{}]{}", e.args.len(), kind))
}

/// 可证明的实参类型：字面量按自身 kind，已声明变量按声明类型；其余 None（不猜）。
fn provable_arg_type(
    e: &Expr,
    declared: &HashMap<String, String>,
    tm: &HashMap<String, String>,
) -> Option<String> {
    // 容器字面量不是叶子，但它的类型**可推导**：`[1,2,3,4]` 是 `[4]int64`。
    // 不推导就得返回 None，而 None 会让调用点整段 `continue`——那正是
    // 「`[3]int64` 收到长度 4 却不报错」的成因（spec 03-类型系统/08 的匹配语义
    // 只管并集形参，非并集形参从未走到那条判定）。
    //
    // 复用 `derive_kind` 的那条推导（`compactarray·new` → `[N]K`），不新建第二套：
    // 元素 kind 不一致或元素不是数值/布尔时推导失败，返回 None 照旧不猜。
    if let Some(t) = container_literal_type(e, tm) {
        return Some(t);
    }
    if !e.is_leaf() {
        return None;
    }
    if e.quote != 0 || matches!(e.lit, LitKind::LitString | LitKind::LitRawString) {
        return Some("char/utf32".to_string());
    }
    match e.lit {
        LitKind::LitBool => return Some("bool".to_string()),
        LitKind::LitInt => return Some("int64".to_string()),
        LitKind::LitFloat => return Some("float64".to_string()),
        _ => {}
    }
    if let Some(t) = declared.get(&e.val) {
        if !t.is_empty() {
            return Some(t.clone());
        }
    }
    let t = literal_slot_type(&e.val);
    if t.is_empty() {
        None
    } else {
        Some(t)
    }
}

fn walk_inst(
    s: &Instruction,
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    declared: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    let Some(e) = &s.expr else { return };
    check_call_args(e, sigs, tm, declared, diags);
    check_implicit_deref_member(e, tm, declared, diags);
    if e.op == format!("struct{}new", keytree::MEMBER_SEP) {
        check_struct_literal(e, structs, diags);
    }
    // 容器元素写：xv·set(container, index, value)。kvspace·set 已死刑（成员写走 =）。
    if (e.op == "compactarray·set") && e.args.len() >= 3 {
        if e.args[0].is_leaf() {
            if let Some(cty) = declared.get(&e.args[0].val) {
                let elem = container_elem(cty);
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

    let annotated = s.write_types.first().map(String::as_str).unwrap_or("");
    let rhs = expr_result_type(e, tm, annotated);
    for (j, w) in s.writes.iter().enumerate() {
        if w.is_empty() {
            return;
        }
        let ann = s.write_types.get(j).map(String::as_str).unwrap_or("");
        let ty = if !ann.is_empty() {
            ann.to_string()
        } else {
            // 无标注的局部变量（`prev = None` 起手、循环里再赋指针）不在 `declared` 里，
            // 只有 `tm` 记录了它的推断类型。`resolve_slot_type` 只查 `declared`，
            // 这里再回退一层 `tm`——否则 `prev = cur` 那条根本不参与校验，
            // 下游 `prev -> *new_head` 才报「值与指针不符」。
            match resolve_slot_type(w, tm, structs, declared)
                .or_else(|| tm.get(w).filter(|t| !t.is_empty()).cloned())
            {
                Some(t) => t,
                None => continue,
            }
        };
        let ty = ty.trim().to_string();
        if ty.is_empty() || ty.contains('|') || ty.starts_with('@') {
            return;
        }
        if matches!(e.op.as_str(), "compactarray·new" | "map·new")
            && (ty.starts_with('[') || ty.contains(keytree::MEMBER_SEP) || ty.starts_with('/'))
        {
            check_container_literal(&ty, e, diags);
        } else {
            // nullable：这个槽的**声明**带 `*`（`ty` 里已剥、要看原始表）。
            let nullable = declared
                .get(w.as_str())
                .or_else(|| tm.get(w.as_str()))
                .is_some_and(|t| t.starts_with('*'));
            check_rhs(&ty, e, &rhs, nullable, diags);
        }
    }
}

/// 调用实参**形状**校验：在 `lower_func` **之前**、在 AST 上做。
///
/// 为什么不能放在 `validate_writes` 里：`flatten_expr`（本文件 993 行附近）会把
/// 嵌套表达式物化成临时槽——`f([1,2,3,4])` 到那时实参只剩 `_1`，容器字面量已经
/// 不存在了，`provable_arg_type` 只能返回 None。于是「`[3]int64` 收到长度 4」永远
/// 走不到 `value_matches`，形状被完全忽略。
///
/// 这一趟只看**能在源码层面证明类型**的实参：字面量与容器字面量。变量实参留给
/// `validate_writes`（那里有 `declared`/`tm`）——两处**共用** `provable_arg_type`
/// 与 `value_matches`，不各写一套判据。
///
/// **失败开放，不是失败封闭**：证明不了类型就放行，不拒绝。所以
/// `x = 1; f(x)`（无注解推断变量）**不会**被形状检查拦——`x` 不在 `declared` 里，
/// `provable_arg_type` 返回 `None`。这是有意的：实参类型推不出来时拒绝，会把每个
/// 没写注解的调用都误拒。要收紧必须先有可靠的局部类型推断，那是另一件事。
/// **不要**把这里改成「推不出来就报错」——那是把不确定当成证据。
///
/// 已知限制：嵌套容器字面量（`[[1,2],[3,4]]`）推导不出来（`container_literal_type`
/// 要求元素是数值/布尔），于是整段跳过。那是**没判**，不是判过——spec 尚未规定嵌套
/// 字面量的形状，属 spec 缺口（见 workspace/622/coding3/shape-matching-fix.md §六）。
pub fn check_call_shapes(
    fn_: &Func,
    sigs: &SigTable,
) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    let empty = HashMap::new();
    walk_call_shapes(&fn_.body, sigs, &empty, &mut diags);
    diags
}

fn walk_call_shapes(
    body: &[Stmt],
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => {
                if let Some(e) = &s.expr {
                    check_one_call_shape(e, sigs, tm, diags);
                }
            }
            Stmt::Scope(s) => walk_call_shapes(&s.body, sigs, tm, diags),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    if let Some(e) = &c.expr {
                        check_one_call_shape(e, sigs, tm, diags);
                    }
                }
                walk_call_shapes(&s.then_, sigs, tm, diags);
                walk_call_shapes(&s.else_, sigs, tm, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    if let Some(e) = &c.expr {
                        check_one_call_shape(e, sigs, tm, diags);
                    }
                }
                walk_call_shapes(&s.body, sigs, tm, diags);
            }
            Stmt::For(s) => walk_call_shapes(&s.body, sigs, tm, diags),
            _ => {}
        }
    }
}

fn check_one_call_shape(
    e: &Expr,
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    let Some(sig) = sig_lookup(sigs, &e.op) else {
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
        let raw = param
            .pointer_type()
            .trim_end_matches("...")
            .trim()
            .to_string();
        if raw.is_empty() || raw.starts_with('@') {
            continue;
        }
        let declared_value_type = deref_value_type(&raw).to_string();
        if !is_matchable_kindexpr(&declared_value_type) {
            continue;
        }
        let Some(vt) = provable_arg_type(arg, &HashMap::new(), tm) else {
            continue;
        };
        if !value_matches(&declared_value_type, &vt) {
            push_write_diag(
                diags,
                format!(
                    "TypeError: cannot pass {vt} to {declared_value_type} (parameter {})",
                    param.name
                ),
            );
        }
    }
    // 实参里也可能嵌着调用：`f(g([1,2,3]))`。
    for arg in &e.args {
        check_one_call_shape(arg, sigs, tm, diags);
    }
}

/// typed 写槽校验（[[赋值转换]]）：可赋性 + 字面量可表示性 + 容器字面量元素。
pub fn validate_writes(
    fn_: &Func,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    sigs: &SigTable,
) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    // 用 `pointer_type()`（带传递方式前缀），与 `declared_types()` 同一来源——
    // 这里曾用 `p.ty.clone()`（剥过前缀），于是 `**N $h` 的 `h` 记成 `/lib/N`，
    // 写侧 `*h` 就比读侧少一层 `*`。**同一事实不能有两份构造**。
    let declared = declared_types(fn_);
    walk_writes(&fn_.body, sigs, tm, structs, &declared, &mut diags);
    diags
}

#[cfg(test)]
mod union_match_tests {
    use super::value_matches;

    #[test]
    fn union_member_matching() {
        assert!(value_matches("int64|float64", "int64"));
        assert!(value_matches("int64|float64", "float64"));
        assert!(!value_matches("int64|float64", "bool"));
        assert!(!value_matches("int64|float64", "int32"));
        assert!(value_matches("int64|float64", "int64|float64"));
        assert!(!value_matches("int64", "int64|float64"));
    }

    #[test]
    fn pointer_and_container_matching() {
        assert!(value_matches("*int64|*float64", "*int64"));
        assert!(value_matches("*int64|int64", "*int64"));
        assert!(!value_matches("*int64", "int64"));
        assert!(value_matches("[]char/utf8|int64", "char/utf32"));
        assert!(value_matches("[2]int64|[3]float64", "[2]int64"));
        assert!(!value_matches("[2]int64", "[3]int64"));
        assert!(value_matches("stringkeymap|int64", "stringkeymap"));
        assert!(value_matches("/lib/P|int64", "/lib/P"));
    }
}

/// 成员访问的基座是 kvlang 指针时，必须显式解引用（spec 05-runtime语义/03-成员访问）。
///
/// parser 只按**形参**判（`ptr_params`），局部变量的类型要到推断之后才知道：
/// `h -> cur` 让 `cur` 是 `*Node`，`cur·val` 就是隐式解引用。这里用 `tm`（推断表）
/// 与 `declared`（声明表）两张表判基座类型，覆盖局部变量。
fn check_implicit_deref_member(
    e: &Expr,
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    if e.op != ast::MEMBER_READ_OP {
        return;
    }
    let Some(base) = e.args.first() else { return };
    // 只认裸名基座：`(*p)·k` 已显式解引用，基座是解引用表达式，不在此列。
    if !base.is_leaf() || base.quote != 0 || base.val.is_empty() {
        return;
    }
    // `*p` 形式的基座同样已显式解引用（parser 已处理形参那条）。
    if base.val.starts_with('*') {
        return;
    }
    // `_N` 是 lower 为复合表达式展开的临时槽：它的来源已由生成它的那条指令判过。
    // 再判一次会把 `(*cur)·val`（展开成 `_1 = *cur; _1·val`）误报成隐式解引用。
    if base.val.starts_with('_') && base.val[1..].chars().all(|c| c.is_ascii_digit()) {
        return;
    }
    let ty = declared
        .get(&base.val)
        .or_else(|| tm.get(&base.val))
        .filter(|t| t.starts_with('*'));
    let Some(ty) = ty else { return };
    // `**T` 的裸名基座本身是 `*T` 值，`·` 作用其上仍是隐式解引用。
    let _ = ty;
    diags.push(Diagnostic {
        pos: Pos { line: 0, col: 0 },
        warn: false,
        info: false,
        message: format!(
            "对指针类型的成员访问必须显式解引用：写 `(*{})·name`",
            base.val
        ),
        source: String::new(),
        src_file: String::new(),
        src_name: String::new(),
    });
}

/// `call_result_type` 的判据：它只做「从实参推/读」，推不出来就 `None`。
///
/// 负向断言是重点——**新规则放过一条本该失败的用例，比它多修 14 条更糟**：
///   ① 四类之外的算子 → None（不新增覆盖）；
///   ② 实参类型不可知 → None（不猜）；
///   ③ 反序列化字面量的目标类型必须过 langtype 判据，不是原样塞。
#[cfg(test)]
mod call_result_type_tests {
    use super::{call_result_type, container_key_type};

    fn types(items: &[&str]) -> Vec<Option<String>> {
        items.iter().map(|item| Some((*item).to_string())).collect()
    }

    fn no_literals() -> Vec<Option<String>> {
        Vec::new()
    }

    #[test]
    fn listat_returns_the_key_type_not_the_value_type() {
        // 键值异型：`[]char/utf32·int64` 的键是字符串、值是 int64。
        // 若这里返回 int64，就是「碰巧在 [int64]·int64 上对」的那个错。
        assert_eq!(
            call_result_type("kvspace·listat", &types(&["[]char/utf32·int64"]), &no_literals()),
            Some("[]char/utf32".to_string())
        );
        assert_eq!(
            call_result_type("kvspace·listat", &types(&["[int64]·int64"]), &no_literals()),
            Some("int64".to_string())
        );
        assert_eq!(
            call_result_type("kvspace·listat", &types(&["int32·int32"]), &no_literals()),
            Some("int32".to_string())
        );
    }

    #[test]
    fn listat_fails_closed_on_shapes_it_cannot_prove() {
        assert_eq!(
            call_result_type("kvspace·listat", &types(&["/lib/Point"]), &no_literals()),
            None
        );
        assert_eq!(
            call_result_type("kvspace·listat", &types(&["[int64,int64]·int64"]), &no_literals()),
            None
        );
        assert_eq!(call_result_type("kvspace·listat", &[None], &no_literals()), None);
        assert_eq!(call_result_type("kvspace·listat", &[], &no_literals()), None);
    }

    #[test]
    fn form_transforms_are_symmetric_and_fail_closed() {
        // compact ↔ 散列：`[]T` ↔ `[int64]·T`
        assert_eq!(
            call_result_type("scatterarray·newscatter", &types(&["[]int64"]), &no_literals()),
            Some("[int64]·int64".to_string())
        );
        assert_eq!(
            call_result_type("scatterarray·newcompact", &types(&["[int64]·int64"]), &no_literals()),
            Some("[]int64".to_string())
        );
        // 已是目标形态 → None，不做隐式跨形态转换
        assert_eq!(
            call_result_type("scatterarray·newscatter", &types(&["[int64]·int64"]), &no_literals()),
            None
        );
        assert_eq!(
            call_result_type("scatterarray·newcompact", &types(&["[]int64"]), &no_literals()),
            None
        );
        // 切片结果与源同型；源不是 compact → None
        assert_eq!(
            call_result_type("compactarray·newslice", &types(&["[]int64"]), &no_literals()),
            Some("[]int64".to_string())
        );
        assert_eq!(
            call_result_type("compactarray·newslice", &types(&["[int64]·int64"]), &no_literals()),
            None
        );
        // 取元素
        assert_eq!(
            call_result_type("compactarray·at", &types(&["[]int64"]), &no_literals()),
            Some("int64".to_string())
        );
    }

    #[test]
    fn reinterpret_reads_the_literal_but_still_validates_it() {
        let with = |text: &str| vec![None, Some(text.to_string())];
        assert_eq!(
            call_result_type("compactarray·newreinterpret", &types(&["[]int64"]), &with("[]uint8")),
            Some("[]uint8".to_string())
        );
        // 非法 langtype：并集、垃圾串、空串 → 一律 None（不许原样塞）
        assert_eq!(
            call_result_type("compactarray·newreinterpret", &types(&["[]int64"]), &with("int64|float64")),
            None
        );
        assert_eq!(
            call_result_type("compactarray·newreinterpret", &types(&["[]int64"]), &with("garbage")),
            None
        );
        assert_eq!(
            call_result_type("compactarray·newreinterpret", &types(&["[]int64"]), &with("")),
            None
        );
        assert_eq!(
            call_result_type("compactarray·newreinterpret", &types(&["[]int64"]), &no_literals()),
            None
        );
    }

    #[test]
    fn operators_outside_the_rule_table_get_no_type() {
        assert_eq!(
            call_result_type("string·concat", &types(&["char/utf32"]), &no_literals()),
            None
        );
        assert_eq!(call_result_type("compactarray·fill", &types(&["[]int64"]), &no_literals()), None);
        assert_eq!(container_key_type("int64"), None);
        assert!(container_key_type("").is_none());
    }
}

#[cfg(test)]
mod none_slot_probe_tests {
    use super::*;

    #[test]
    fn deref_parenreaches_the_deref_branch() {
        let mut declared = HashMap::new();
        declared.insert("r".to_string(), "*[]char/utf32".to_string());
        let structs = StructTable::default();
        /* 改前：`(*r)` 被判成普通名 → declared 里没有 `(*r)` → None。
         * 改后：走 `(*p)` 分支 → 剥一层 `*` → Some("[]char/utf32")。 */
        assert_eq!(
            resolve_slot_type("(*r)", &HashMap::new(), &structs, &declared),
            Some("[]char/utf32".to_string())
        );
        /* 别的括号形态不受影响：`(x)` 仍按普通名查，查不到就是 None。 */
        assert_eq!(
            resolve_slot_type("(x)", &HashMap::new(), &structs, &declared),
            None
        );
    }
}
