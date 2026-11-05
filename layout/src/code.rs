//! layoutcode（对齐 layout/layout.go）：检查 AST 并把结果布局写到 /lib/ 下的结构化 KV。
//!
//! 存储约定：
//!   /lib/<pkg>·<name>/[0,0]         布局后签名锚点（kind=rwfunc，body=计数头 [nr,nw,dyn]）
//!   /lib/<pkg>·<name>.[0,±k]        参数定义键（langtype=def langtype, body=名字\x00类型）
//!   /lib/<pkg>·<name>/[i,j]         编译后指令（kind=rwir），i 从 1 开始
//!   /lib/<pkg>·<name>/‥labels/<l>   label → irseq
//!   /lib/<pkg>·<name>.src           源码副本（仅 write_func 保留写入，printlib 不再依赖）
//!
//! WriteBody: DFS-number insts (incl. ScopeStmt), emit [i,j], rewrite goto/br labels to irseq.
//! printlib: 反向——严格读 /lib/<pkg> 子树重建 AST（签名读参数定义键 .[0,±k]、体读线性槽+‥labels），
//!       不读 .src、不依赖签名行 [0,x] 静态槽。printstack: /vthread/<vid> 活动栈渲染（只读）。

use std::collections::{HashMap, HashSet};

use super::ast::{
    self, Expr, Func, FuncSig, Instruction, Param, RwirDecl, ScopeStmt, Stmt, StructDecl,
};
use super::ffi::Kv;
use super::{builtin, ffi, keytree, kvkind, lower, parser};

/// 创建基础目录 /lib/ 与 /vthread/（layout 前必须存在）。
pub fn init_dirs(kv: &mut Kv) -> Result<(), String> {
    kv.mkindex("/lib/")?;
    kv.mkindex("/vthread/")?;
    Ok(())
}

/// 顶层入口（对齐 cmd/kvlang/layout.go 的 cmdLayout）：parse → lower → write。
/// 返回本次写入的 init 函数名列表（pkg 严格取自 `lib` 声明：`lib X {…}` → `X·init`，
/// 裸顶层语句 → `init`），供消费方按 lib 声明驱动 init（非文件系统路径推导）。
pub fn compile(kv: &mut Kv, src: &str) -> Result<Vec<String>, String> {
    // Run all pure checks first (shared with `vet`) so errors never leave partial writes.
    let checked = analyze_file(src)?;
    let file = &checked.file;

    let mut inits: Vec<String> = Vec::new();
    let local_funcs: HashSet<String> = file
        .funcs
        .iter()
        .map(|f| {
            let pkg = if f.pkg.is_empty() {
                &file.package
            } else {
                &f.pkg
            };
            if pkg.is_empty() {
                f.sig.name.clone()
            } else {
                format!("{pkg}{}{name}", keytree::MEMBER_SEP, name = f.sig.name)
            }
        })
        .collect();
    // Write order: struct decls -> rwfuncs -> rwir decls -> top-level init.
    for decl in &file.structs {
        write_struct_decl(kv, decl)?;
    }
    for a in &checked.funcs {
        write_func(kv, a, &local_funcs, &checked.structs, &checked.sigs)?;
        if a.is_init {
            inits.push(init_fn_name(&a.pkg));
        }
    }
    for decl in &file.rwir_decls {
        write_rwir_decl(kv, decl)?;
    }
    if let Some(a) = &checked.top_init {
        write_func(kv, a, &local_funcs, &checked.structs, &checked.sigs)?;
        inits.push(init_fn_name(""));
    }
    Ok(inits)
}

/// Pure per-function analysis output: lowering, slot/type checks, specialization, and linearization.
pub struct AnalyzedFunc {
    pkg: String,
    func: Func,
    declared: HashMap<String, String>,
    type_sets: HashMap<String, Vec<String>>,
    /// 赋值来源可证明的具体 kind（见 `lower::stable_kinds`）。opcode 不再携带
    /// 类型后，读取层靠槽上的具体 langtype 收敛；声明的 def langtype 可能是并集，
    /// 这里优先写可证明的具体 kind。
    stable: HashMap<String, String>,
    seq: Vec<Instruction>,
    labels: HashMap<String, i32>,
    is_init: bool,
}

/// Output of [`analyze_file`]: parsed AST plus analyzed rwfuncs and optional top-level init.
pub struct CheckedFile {
    file: ast::File,
    structs: lower::StructTable,
    /// 文件级签名表：写指令槽时用来取**被调方形参类型**（`None` 作实参时的
    /// 上下文类型只能从这里得到）。
    sigs: lower::SigTable,
    funcs: Vec<AnalyzedFunc>,
    top_init: Option<AnalyzedFunc>,
}

/*
 * 诊断阶段的**唯一失败收尾**：先打印全部诊断，再构造返回给调用方的错误串。
 *
 * **返回串必须带首条 error 级诊断**，不能只给一句汇总。
 *
 * 为什么：CLI 用户看得到 `eprintln!` 打出的具体原因，但 **C ABI 调用方看不到**——
 * `kvlangLayoutCode` / `kvlangLayoutFile` 只把本函数的 `Err` 文本写进 `err_out`。
 * 只给「refusing to load」等于让嵌入式与 LLM 工具链拿到一个**没有原因的失败**，
 * 只能靠猜。同一个失败，两拨人看到的信息量不该差这么远。
 *
 * 打印仍然保留（CLI 的 stderr 输出行为不变），返回串追加摘要。
 */
fn refuse_with_diagnostics(diags: &[crate::scanner::Diagnostic], stage: &str) -> String {
    for d in diags {
        eprintln!("{}", d.string());
    }
    let first = diags
        .iter()
        .find(|d| !d.warn)
        .map(|d| d.string())
        .unwrap_or_else(|| stage.to_string());
    let errors = diags.iter().filter(|d| !d.warn).count();
    if errors > 1 {
        format!("{stage}: error-level diagnostics — refusing to load ({first}; and {} more)", errors - 1)
    } else {
        format!("{stage}: error-level diagnostics — refusing to load ({first})")
    }
}

/// Pure kvspace-free validation shared by `vet` and `compile`: parse/container diagnostics,
/// lowering, write-slot checks, specialization, linearization, jump target resolution, and op arity.
/// State-dependent checks (such as `/lib/<opcode>` `def rwir` collisions) remain in `compile`.
fn analyze_file(src: &str) -> Result<CheckedFile, String> {
    let (file, mut diags) = parser::parse_code(src)?;
    for f in subscript_check_funcs(&file) {
        diags.extend(lower::check_container_subscript(&f));
        diags.extend(lower::check_map_defined(&f));
        diags.extend(lower::check_container_typed(&f));
    }
    if parser::has_errors(&diags) {
        return Err(refuse_with_diagnostics(&diags, "parse"));
    }
    for d in &diags {
        eprintln!("{}", d.string());
    }
    let mut sigs: lower::SigTable = HashMap::new();
    for f in &file.funcs {
        let qual = if f.pkg.is_empty() {
            f.sig.name.clone()
        } else {
            format!("{}{}{}", f.pkg, keytree::MEMBER_SEP, f.sig.name)
        };
        sigs.entry(qual).or_insert_with(|| f.sig.clone());
    }
    for d in &file.rwir_decls {
        let qual = if d.pkg.is_empty() {
            d.sig.name.clone()
        } else {
            format!("{}{}{}", d.pkg, keytree::MEMBER_SEP, d.sig.name)
        };
        sigs.entry(qual).or_insert_with(|| d.sig.clone());
    }
    let mut structs: lower::StructTable = HashMap::new();
    for decl in &file.structs {
        let qual = if decl.pkg.is_empty() {
            decl.name.clone()
        } else {
            format!("{}{}{}", decl.pkg, keytree::MEMBER_SEP, decl.name)
        };
        structs.entry(qual).or_insert_with(|| decl.clone());
        structs
            .entry(decl.name.clone())
            .or_insert_with(|| decl.clone());
    }
    let mut funcs: Vec<AnalyzedFunc> = Vec::new();
    for func in &file.funcs {
        let pkg = if func.pkg.is_empty() {
            file.package.clone()
        } else {
            func.pkg.clone()
        };
        funcs.push(analyze_func(func, pkg, &structs, &sigs)?);
    }
    let mut body = file.init_body.clone();
    for c in &file.top_level_calls {
        body.push(Stmt::Instruction(c.clone()));
    }
    let mut top_init = None;
    if !body.is_empty() {
        let init_fn = Func {
            comments: Vec::new(),
            sig: super::ast::FuncSig {
                name: "init".to_string(),
                params: Vec::new(),
                returns: Vec::new(),
            },
            body,
            pkg: String::new(),
        };
        top_init = Some(analyze_func(&init_fn, String::new(), &structs, &sigs)?);
    }
    if funcs.is_empty()
        && top_init.is_none()
        && file.structs.is_empty()
        && file.rwir_decls.is_empty()
    {
        return Err("no executable code found".to_string());
    }
    Ok(CheckedFile {
        file,
        structs,
        sigs,
        funcs,
        top_init,
    })
}

/// Pure single-function analysis: call-return annotation, lowering, linearization, and jump validation.
fn analyze_func(
    func: &Func,
    pkg: String,
    structs: &lower::StructTable,
    sigs: &lower::SigTable,
) -> Result<AnalyzedFunc, String> {
    // Annotate call-site return types from `sigs` before lowering.
    let mut src = func.clone();
    lower::annotate_call_returns(&mut src, sigs);
    // 形状校验必须在 lower **之前**：`flatten_expr` 会把 `f([1,2,3,4])` 的实参
    // 物化成临时槽，此后容器字面量不复存在，形状再也比不了。
    let mut shape_diags = lower::check_call_shapes(&src, sigs);
    let mut fn_ = lower::lower_func(&src)?;
    let type_map = lower::infer_types(&fn_);
    // 合成表：形参行是 Value 形态、写槽行是源级形态；优先级见
    // `lower::declared_langtypes_by_origin` 的注释（形参优先，等价性条件）。
    let declared = lower::declared_langtypes_by_origin(&fn_);
    let type_sets = lower::infer_type_sets(&fn_);
    let mut diags = std::mem::take(&mut shape_diags);
    diags.extend(lower::validate_writes(&fn_, &type_map, structs, sigs));
    /* 推断必须在 specialize 之前算一次，且只算一次：specialize 只改 e.op 的字形
     * （`+` → `add`）与 `operator`/`operand_kind`，不改赋值来源与类型，所以两处
     * 结果相同；在它之后重算既多余，又逼着 `exact_kind` 同时认两种 op 形态。
     * 两个证据来源：`stable_kinds` 认声明为单一具体类型的变量，
     * `assigned_concrete_kinds` 认「声明是并集、但每次赋值都是同一具体 kind」的
     * 变量——后者是 encoder 收敛的关键。 */
    let mut stable = lower::stable_kinds(&fn_, &type_map, &declared);
    for (name, kind) in lower::assigned_concrete_kinds(&fn_, &declared) {
        stable.entry(name).or_insert(kind);
    }
    diags.extend(lower::specialize(
        &mut fn_, &type_map, &declared, &type_sets, &stable,
    ));
    if parser::has_errors(&diags) {
        return Err(refuse_with_diagnostics(&diags, "type"));
    }
    for d in &diags {
        eprintln!("{}", d.string());
    }
    let mut seq: Vec<Instruction> = Vec::new();
    let mut labels: HashMap<String, i32> = HashMap::new();
    collect_insts(&fn_.body, &mut seq, &mut labels)
        .map_err(|e| format!("{}: {e}", fn_.sig.name))?;
    check_jumps(&seq, &labels).map_err(|e| format!("{}: {e}", fn_.sig.name))?;
    /* lower 之后补写槽类型：A/B/D 三类缺口共用这一个入口。必须在 write_func 之前、
     * 在 seq 定稿之后——早于 lower 时临时槽还没展开。
     *
     * 同时把推导结果并进 `stable`（读侧最后一个来源），让写槽与后续读槽共用同一份
     * 结论。**这必须在 `specialize` 之后**：`specialize` 只看 op/操作数形态与
     * 声明类型，不看槽类型；一旦将来有特化规则需要槽类型，合并点要再前移，
     * 而不是让 `specialize` 自己去补查——那会造出第二个查询面。 */
    annotate_slot_types(&mut seq, &declared, &type_sets, &mut stable, sigs, structs);
    // Validate instruction arity before touching kvspace (shared by `vet` and `compile`).
    for inst in &seq {
        let (op, reads) = inst.flat();
        if op.is_empty() {
            continue;
        }
        check_sub_arity(&op, reads.len(), inst.writes.len())
            .map_err(|e| format!("{}: {e}", fn_.sig.name))?;
    }
    Ok(AnalyzedFunc {
        is_init: func.sig.name == "init",
        pkg,
        func: fn_,
        declared,
        type_sets,
        stable,
        seq,
        labels,
    })
}

/// 待做 `[]` 下标校验的函数集合：显式 funcs + init_body/top_level_calls 合成的 init。
fn subscript_check_funcs(file: &super::ast::File) -> Vec<Func> {
    let mut funcs: Vec<Func> = file.funcs.clone();
    let mut body = file.init_body.clone();
    for c in &file.top_level_calls {
        body.push(Stmt::Instruction(c.clone()));
    }
    if !body.is_empty() {
        funcs.push(Func {
            comments: Vec::new(),
            sig: super::ast::FuncSig {
                name: "init".to_string(),
                params: Vec::new(),
                returns: Vec::new(),
            },
            body,
            pkg: String::new(),
        });
    }
    funcs
}

/// pkg → init 函数名（运行时用名，无 /lib 前缀）：空 pkg → `init`，否则 `<pkg>·init`。
fn init_fn_name(pkg: &str) -> String {
    if pkg.is_empty() {
        "init".to_string()
    } else {
        format!("{pkg}{}init", keytree::MEMBER_SEP)
    }
}

/// 格式化源码（parse → 规范化源码），不写入 kvspace。失败返回错误。
pub fn format(src: &str) -> Result<String, String> {
    let (file, diags) = parser::parse_code(src)?;
    for d in &diags {
        eprintln!("{}", d.string());
    }
    if parser::has_errors(&diags) {
        return Err("parse: error-level diagnostics — refusing to format".to_string());
    }
    Ok(file.format())
}

/// 校验源码是否可 layout（parse + lower），但不写入 kvspace。
/// 供运行时 vet 闸门：LLM 生成的 kv 代码先过此关，失败不污染 /lib。
///
/// **不覆盖依赖 `/lib` 既有声明的检查**（`check_call_names` 的「裸名是否已被声明为 rwir」）——
/// 没有 store 就无从判定。要那份结论请用 [`vet_with_store`]。这条差别必须显式存在：
/// 否则「vet 通过」会被当成「能编译」，而 `println` 这类由 runtime 在 boot 时发布 def rwir
/// 的算子，在 `vet` 下与 `compile` 下会给出**相反**的结论。
pub fn vet(src: &str) -> Result<(), String> {
    // Shares `analyze_file` with `compile`; state-dependent checks run only in `compile`.
    analyze_file(src).map(|_| ())
}

/// 带 store 的校验：在 [`vet`] 的纯检查之上，再跑一遍与 `compile` **同一条**调用名检查。
///
/// `store` 里必须有被调算子的声明（如 runtime boot 时发布的 `def rwir`），
/// 否则结论与 `compile` 相同——两者用同一个 `check_call_names`，不另立判据。
pub fn vet_with_store(kv: &mut Kv, src: &str) -> Result<(), String> {
    let checked = analyze_file(src)?;
    let file = &checked.file;
    let local_funcs: HashSet<String> = file
        .funcs
        .iter()
        .map(|f| {
            let pkg = if f.pkg.is_empty() {
                &file.package
            } else {
                &f.pkg
            };
            if pkg.is_empty() {
                f.sig.name.clone()
            } else {
                format!("{pkg}{}{name}", keytree::MEMBER_SEP, name = f.sig.name)
            }
        })
        .collect();
    for a in &checked.funcs {
        check_call_names(kv, &a.pkg, &a.seq, &local_funcs)?;
    }
    if let Some(a) = &checked.top_init {
        check_call_names(kv, "", &a.seq, &local_funcs)?;
    }
    Ok(())
}

/// printlib：把 /lib 子树重构为可运行的 kvlang 源码（**只看 layout 结果，不读 `.src`**：
/// 签名读参数定义键、函数体读线性指令槽），lower 后的原始槽位（`key kind:value`）
/// 以 `#` 注释附在各自函数后，供审查。
pub fn printlib(kv: &mut Kv, lib: &str) -> String {
    // lib 是 /lib 下任意 prefix，只 dump 该子树。三种情形：
    //   /lib           → 全量
    //   /lib/foo       → 虚拟 pkg（func 存于 /lib/foo·* 扁平目录）：走全树后过滤
    //   /lib/foo·main  → 精确函数目录：直接 emit 该函数
    let prefix = lib.trim_end_matches('/').to_string();
    let mut funcs: Vec<DumpFunc> = Vec::new();
    let mut decls: Vec<String> = Vec::new();
    if prefix == "/lib" {
        collect_funcs(kv, "/lib/", "", &mut funcs, &mut decls);
    } else if is_func_dir(kv, &format!("{prefix}/")) {
        // prefix 本身就是函数目录：直接重建该函数（pkg/name 从路径反推）。
        let base = prefix.trim_start_matches("/lib/");
        let (fpkg, name) = func_identity("", base);
        let dir = format!("{prefix}/");
        let text = reconstruct(kv, &dir, &name);
        let mut slots = Vec::new();
        collect_slots(kv, &dir, &mut slots);
        funcs.push(DumpFunc {
            pkg: fpkg,
            name,
            text,
            slots,
            dir,
        });
    } else {
        // 虚拟 pkg：func dir 以 prefix 为前缀（`/lib/foo` 匹配 `/lib/foo·*` 与 `/lib/foo/*`）。
        collect_funcs(kv, "/lib/", "", &mut funcs, &mut decls);
        let under = |dir: &str| {
            dir.starts_with(&format!("{prefix}·")) || dir.starts_with(&format!("{prefix}/"))
        };
        funcs.retain(|f| under(&f.dir));
        decls.retain(|d| {
            let path = d.split(' ').next().unwrap_or("");
            under(path)
        });
    }

    let mut root = DumpNode::default();
    for f in funcs {
        pkg_node(&mut root, &f.pkg).funcs.push(f);
    }
    let mut out = String::new();
    emit_node(&mut out, &root, "");
    for d in decls {
        out.push_str("// ");
        out.push_str(&d);
        out.push('\n');
    }
    out
}

/// 一个可运行函数：从 /lib 子树重建的源码 + 原始槽位注释。
struct DumpFunc {
    pkg: String,
    name: String,
    text: String,
    slots: Vec<String>,
    dir: String,
}

/// pkg 树节点（与 ast 的 PkgNode 同构，但装 dump 产物）。
#[derive(Default)]
struct DumpNode {
    funcs: Vec<DumpFunc>,
    children: std::collections::BTreeMap<String, DumpNode>,
}

fn pkg_node<'a>(root: &'a mut DumpNode, pkg: &str) -> &'a mut DumpNode {
    if pkg.is_empty() {
        return root;
    }
    let mut cur = root;
    for seg in pkg.split('/') {
        cur = cur.children.entry(seg.to_string()).or_default();
    }
    cur
}

/// 递归收集 /lib 下的函数（目录 + 同名 .src）与 defrwir 声明。pkg 用 / 分隔累积。
/// 目录判定不依赖 list 的尾斜杠（shm 后端不带、redis 带）——用 is_func_dir / 子项非空 探测。
fn collect_funcs(
    kv: &mut Kv,
    prefix: &str,
    pkg: &str,
    funcs: &mut Vec<DumpFunc>,
    decls: &mut Vec<String>,
) {
    for c in kv.list(prefix, false, true) {
        if c.ends_with(keytree::SRC_EXT) {
            // .src 与其目录成对，随目录处理，此处跳过。
            continue;
        }
        let base = c.trim_end_matches('/').to_string();
        let sub_pkg = if pkg.is_empty() {
            base.clone()
        } else {
            format!("{pkg}/{base}")
        };
        // 函数目录（含 [0,0] 签名槽）→ 重建源码；普通目录 → 递归；成员容器（· 结尾）→ 递归；否则声明叶。
        let dir_sub = format!("{prefix}{base}/");
        let mem_sub = format!("{prefix}{base}·");
        if is_func_dir(kv, &dir_sub) {
            let (fpkg, name) = func_identity(pkg, &base);
            let text = reconstruct(kv, &dir_sub, &name);
            let mut slots = Vec::new();
            collect_slots(kv, &dir_sub, &mut slots);
            funcs.push(DumpFunc {
                pkg: fpkg,
                name,
                text,
                slots,
                dir: dir_sub,
            });
        } else if !kv.list(&dir_sub, false, true).is_empty() {
            collect_funcs(kv, &dir_sub, &sub_pkg, funcs, decls);
        } else if !kv.list(&mem_sub, false, true).is_empty() {
            collect_funcs(kv, &mem_sub, &sub_pkg, funcs, decls);
        } else {
            let key = format!("{prefix}{base}");
            let v = kv.get_one(&key);
            decls.push(format!("{key} {}", sanitize(&kvkind::display(&v, &kv.langtype_of(&key)))));
        }
    }
}

/// 目录是否为函数目录（含 [0,0] 签名槽）。lib 目录只有子函数/子 lib，无 [ 槽位。
fn is_func_dir(kv: &mut Kv, dir: &str) -> bool {
    kv.list(dir, false, true).iter().any(|c| c.starts_with('['))
}

/// 目录名 → (pkg, name)。扁平后端（fs）目录名含 `·`（`<pkg尾段>·<name>`）需拆；
/// 嵌套后端（redis memindex）目录名已是裸函数名，直接沿用累积 pkg。
fn func_identity(pkg: &str, base: &str) -> (String, String) {
    match base.rfind(keytree::MEMBER_SEP) {
        Some(i) => {
            let seg = &base[..i];
            let name = &base[i + keytree::MEMBER_SEP.len()..];
            let fpkg = if pkg.is_empty() {
                seg.to_string()
            } else {
                format!("{pkg}/{seg}")
            };
            (fpkg, name.to_string())
        }
        None => (pkg.to_string(), base.to_string()),
    }
}

/// 递归导出函数目录下的槽位行（相对 key + kind:value，跳过空索引目录）。
fn collect_slots(kv: &mut Kv, prefix: &str, out: &mut Vec<String>) {
    for c in kv.list(prefix, false, true) {
        let full = format!("{prefix}{c}");
        if c.ends_with('/') {
            collect_slots(kv, &full, out);
        } else {
            let v = kv.get_one(&full);
            out.push(format!("{c} {}", sanitize(&kvkind::display(&v, &kv.langtype_of(&full)))));
        }
    }
}

/// 注释不允许换行：rwfunc 签名的参数类型以 \n 连接，改 " | " 呈现。
fn sanitize(s: &str) -> String {
    s.replace('\n', " | ")
}

fn emit_node(out: &mut String, node: &DumpNode, indent: &str) {
    let mut funcs: Vec<&DumpFunc> = node.funcs.iter().collect();
    funcs.sort_by(|a, b| a.name.cmp(&b.name));
    for f in funcs {
        emit_func(out, f, indent);
    }
    for (name, child) in &node.children {
        out.push_str(indent);
        out.push_str("lib ");
        out.push_str(name);
        out.push_str(" {\n");
        emit_node(out, child, &format!("{indent}    "));
        out.push_str(indent);
        out.push_str("}\n");
    }
}

fn emit_func(out: &mut String, f: &DumpFunc, indent: &str) {
    for line in f.text.lines() {
        out.push_str(indent);
        out.push_str(line);
        out.push('\n');
    }
    out.push_str(indent);
    out.push_str("// ");
    out.push_str(&f.dir);
    out.push('\n');
    for s in &f.slots {
        out.push_str(indent);
        out.push_str("//   ");
        out.push_str(s);
        out.push('\n');
    }
    out.push('\n');
}

// ── printlib 重建：严格从 /lib 子树反出可运行 kvlang（**不读 .src**）───────
//
// 数据来源（对齐 write_func / spec「指令布局格式」）：
//   签名  ← [0,0] 计数头(nr,nw,dyn) + 命名参数 Ptr 键（langtype=类型、body=[0,±k] 定位读/写与序）
//   函数体 ← 线性指令槽 [n,0]=opcode、[n,-j]=读参、[n,j]=写参（n 连续、scope 已拍平）
//   控制流 ← ‥labels/<label>=irseq；goto/br 的整数读参即 irseq，映射回 label 名并按 irseq 切块
// 重建成 AST(Func) 后复用其 Display/full_text，箭头一律规范化为 `->`（源箭头风格不落盘）。

/// 单个函数目录 → 可运行 kvlang 文本（签名 + 体）。
fn reconstruct(kv: &mut Kv, dir: &str, name: &str) -> String {
    let (nr, nw, dynamic) = kvkind::counts(&kv.get_one(&format!("{dir}[0,0]")));
    let sig = reconstruct_sig(kv, dir, name, nr, nw, dynamic);
    let labels = read_labels(kv, dir);
    let insts = read_insts(kv, dir);
    let body = build_body(&insts, &labels);
    Func {
        comments: Vec::new(),
        sig,
        body,
        pkg: String::new(),
    }
    .full_text()
}

/// 从参数定义键 base.[0,±k]（点后缀）重建签名：body=名字\x00类型串。
fn reconstruct_sig(kv: &mut Kv, dir: &str, name: &str, nr: i32, nw: i32, dynamic: bool) -> FuncSig {
    let blank = || Param {
        name: String::new(),
        ty: String::new(),
        by_ref: String::new(),
        has_ty: false,
    };
    let mut params: Vec<Param> = (0..nr).map(|_| blank()).collect();
    let mut returns: Vec<Param> = (0..nw).map(|_| blank()).collect();
    let base = dir.trim_end_matches('/');
    for k in 1..=nr {
        if let Some((pname, pty)) =
            kvkind::def_param_parts(&kv.get_one(&format!("{base}.[0,-{k}]")))
        {
            params[(k - 1) as usize] = Param {
                name: pname,
                ty: pty,
                by_ref: String::new(),
                has_ty: true,
            };
        }
    }
    for k in 1..=nw {
        if let Some((rname, rty)) = kvkind::def_param_parts(&kv.get_one(&format!("{base}.[0,{k}]")))
        {
            returns[(k - 1) as usize] = Param {
                name: rname,
                ty: rty,
                by_ref: String::new(),
                has_ty: true,
            };
        }
    }
    if dynamic {
        if let Some(p) = params.last_mut() {
            if !p.ty.is_empty() {
                p.ty.push_str("...");
            }
        }
    }
    FuncSig {
        name: name.to_string(),
        params,
        returns,
    }
}

/// ‥labels/ 子树 → (irseq, label)，按 irseq 升序（体切块用）。
fn read_labels(kv: &mut Kv, dir: &str) -> Vec<(i32, String)> {
    let ldir = format!(
        "{dir}{}{}/",
        keytree::RUNTIME_MEMBER_SEP,
        keytree::SEG_LABELS
    );
    let mut out: Vec<(i32, String)> = kv
        .list(&ldir, false, true)
        .into_iter()
        .filter(|c| !c.ends_with('/'))
        // `‥labels/` 是个目录，目录自己的元数据键也在 list 结果里（`__dir__`），
        // 它不是 label：它的 body 是目录头，按 int64 解会得到垃圾 irseq，
        // 拿去切片就是越界 panic。
        .filter(|c| !c.starts_with("__"))
        .filter_map(|c| {
            let key = format!("{ldir}{c}");
            let irseq: i32 = kvkind::plain(&kv.get_one(&key), &kv.langtype_of(&key))
                .parse()
                .ok()?;
            Some((irseq, c))
        })
        .collect();
    out.sort_by_key(|(irseq, _)| *irseq);
    out
}

/// 一条重建指令：opcode + 读操作数 + 写目标名。
struct RawInst {
    opcode: String,
    reads: Vec<Operand>,
    writes: Vec<String>,
    write_types: Vec<String>,
}

/// 操作数：引用（变量/opcode/路径）、字符串字面量、其它字面量（数值/bool）。
enum Operand {
    Ref(String),
    Str(String),
    Lit(String),
}

/// 线性读回 [n,*]（n 从 1 连续到首个空 opcode 前）。
fn read_insts(kv: &mut Kv, dir: &str) -> Vec<RawInst> {
    let mut out = Vec::new();
    let mut n = 1;
    loop {
        let op = kv.get_one(&format!("{dir}[{n},0]"));
        if op.is_empty() {
            break;
        }
        let opcode = kvkind::rwir_sig(&op);
        let mut reads = Vec::new();
        let mut j = 1;
        loop {
            let key = format!("{dir}[{n},-{j}]");
            let d = kv.get_one(&key);
            if d.is_empty() {
                break;
            }
            reads.push(operand_from_wire(&d, &kv.langtype_of(&key)));
            j += 1;
        }
        let mut writes = Vec::new();
        let mut wtypes = Vec::new();
        let mut j = 1;
        loop {
            let d = kv.get_one(&format!("{dir}[{n},{j}]"));
            if d.is_empty() {
                break;
            }
            let (w, ty) = kvkind::write_slot_name(&d);
            writes.push(w);
            wtypes.push(ty);
            j += 1;
        }
        out.push(RawInst {
            opcode,
            reads,
            writes,
            write_types: wtypes,
        });
        n += 1;
    }
    out
}

/// 槽值 → Operand：rwir 族为引用/opcode 名，char 为字符串字面量，其余为明文字面量。
fn operand_from_wire(data: &[u8], lt: &str) -> Operand {
    let k = kvkind::kind(data, lt);
    if matches!(k.as_str(), "rwir" | "rwfunc" | "def rwir") {
        Operand::Ref(kvkind::rwir_sig(data))
    } else if kvkind::is_char_kind(&k) {
        Operand::Str(kvkind::plain(data, lt))
    } else {
        Operand::Lit(kvkind::plain(data, lt))
    }
}

fn operand_expr(o: &Operand) -> Expr {
    match o {
        Operand::Ref(s) | Operand::Lit(s) => ast::leaf(s),
        Operand::Str(s) => ast::str_lit(s),
    }
}

/// goto/br 的整数读参 → label 名（查不到则原样保留数字）。
fn label_leaf(o: &Operand, by_irseq: &HashMap<i32, String>) -> Expr {
    if let Operand::Lit(s) = o {
        if let Ok(n) = s.parse::<i32>() {
            if let Some(l) = by_irseq.get(&n) {
                return ast::leaf(l);
            }
        }
    }
    operand_expr(o)
}

/// 线性指令 + labels → 语句序列：首 label 前为前导语句，各 label 段成 ScopeStmt。
///
/// `‥labels/<name>` 的 irseq 是**外部可写**的数据（kvspace 里一个普通键），越界或倒序
/// 都不是「可以夹带过去」的情形：越界会切片越界 panic，倒序会静默丢指令段。两者都
/// 说明这份 `/lib` 与它的 label 表不一致，按 p0 直接 panic 并指认具体 label。
fn build_body(insts: &[RawInst], labels: &[(i32, String)]) -> Vec<Stmt> {
    let by_irseq: HashMap<i32, String> = labels.iter().map(|(i, l)| (*i, l.clone())).collect();
    let total = insts.len() as i32;
    // 先整体校验，再切片：落在 `Range::map` 里的断言只能看见实际迭代到的下标，
    // 越界的 label 会让它自己的区间变空从而整个跳过，等于没检查。
    for (bi, (start, label)) in labels.iter().enumerate() {
        assert!(
            (1..=total).contains(start),
            "label {label:?} 的 irseq {start} 超出指令范围 1..={total}"
        );
        if let Some((next, _)) = labels.get(bi + 1) {
            assert!(
                start < next,
                "label {label:?} 的 irseq {start} 不小于其后 label 的 {next}（倒序或重复）"
            );
        }
    }
    let first = labels.first().map(|(i, _)| *i).unwrap_or(total + 1);
    let inst_stmt = |i: i32| {
        assert!(
            (1..=total).contains(&i),
            "指令下标 {i} 超出范围 1..={total}"
        );
        Stmt::Instruction(build_inst(&insts[i as usize - 1], &by_irseq))
    };
    let mut body: Vec<Stmt> = (1..first).map(inst_stmt).collect();
    for (bi, (start, label)) in labels.iter().enumerate() {
        let end = labels.get(bi + 1).map(|(i, _)| *i).unwrap_or(total + 1);
        body.push(Stmt::Scope(ScopeStmt {
            comments: Vec::new(),
            label: label.clone(),
            body: (*start..end).map(inst_stmt).collect(),
            synthetic: true,
        }));
    }
    body
}

/// (opcode, reads, writes) → Instruction（箭头规范化为 `->`；复用 Display 还原算子/糖）。
fn build_inst(inst: &RawInst, by_irseq: &HashMap<i32, String>) -> Instruction {
    let expr = match inst.opcode.as_str() {
        "" => None,
        "return" => Some(ast::leaf("return")),
        // Single read is plain copy; >=2 reads reconstruct `MEMBER_READ_OP(base, seg...)`.
        "=" if inst.reads.len() > 1 => Some(ast::call(
            ast::MEMBER_READ_OP,
            inst.reads.iter().map(operand_expr).collect(),
        )),
        "=" => inst.reads.first().map(operand_expr),
        "goto" => Some(ast::call(
            "goto",
            inst.reads.iter().map(|o| label_leaf(o, by_irseq)).collect(),
        )),
        "br" => {
            let mut args = Vec::with_capacity(inst.reads.len());
            for (i, o) in inst.reads.iter().enumerate() {
                args.push(if i == 0 {
                    operand_expr(o)
                } else {
                    label_leaf(o, by_irseq)
                });
            }
            Some(ast::call("br", args))
        }
        op => Some(ast::call(op, inst.reads.iter().map(operand_expr).collect())),
    };
    // array·fill 首参即写目标 langtype（Display 隐去不回显），回填 write_types 恢复类型标注。
    // 且须用前置 `=` 式（arrow_left=true）：parser 仅在前置标注式把写类型喂给 array·fill 首参，
    // 后置箭头式 `[5...] -> b:TYPE` 会丢弃该类型（parser 不对称），故这里强制前置式以保幂等。
    let is_fill = inst.opcode == "compactarray·fill" && !inst.writes.is_empty();
    let write_types = if is_fill {
        let ty = match inst.reads.first() {
            Some(Operand::Str(s)) | Some(Operand::Ref(s)) | Some(Operand::Lit(s)) => s.clone(),
            None => String::new(),
        };
        let mut v = vec![String::new(); inst.writes.len()];
        v[0] = ty;
        v
    } else {
        let mut v = inst.write_types.clone();
        if v.iter().all(String::is_empty) {
            v = Vec::new();
        }
        v
    };
    Instruction {
        comments: Vec::new(),
        expr,
        writes: inst.writes.clone(),
        write_types,
        arrow_left: is_fill || inst.write_types.iter().any(|t| !t.is_empty()),
        operator: false,
        operand_kind: String::new(),
    }
}

/// 通用 `sub` 只能是 2r→1w；一元取负必须走 `neg/abs`（P1b2 契约）。
pub fn check_sub_arity(op: &str, reads: usize, writes: usize) -> Result<(), String> {
    if op == "sub" && (reads != 2 || writes != 1) {
        return Err(format!(
            "sub must emit 2 reads / 1 write (got {reads}/{writes})"
        ));
    }
    Ok(())
}

fn read_ctime(kv: &mut Kv, key: &str) -> i64 {
    let v = kv.get_one(key);
    if v.is_empty() || ffi::xh_is_none(&v) {
        return 0;
    }
    let b = ffi::xh_body_slice(&v);
    if b.len() < 8 {
        return 0;
    }
    i64::from_le_bytes(b[..8].try_into().unwrap())
}

fn next_ctime(old: i64) -> i64 {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos() as i64)
        .unwrap_or(0);
    old.saturating_add(1).max(now)
}

/// 写函数到 /lib/：签名（rwfunc）、源码、参数 Ptr、指令体。
/// Check kvspace-dependent collisions (`def rwir` vs `rwfunc`) before deleting the old subtree and writing.
pub fn write_func(
    kv: &mut Kv,
    a: &AnalyzedFunc,
    local_funcs: &HashSet<String>,
    structs: &lower::StructTable,
    sigs: &lower::SigTable,
) -> Result<(), String> {
    let pkg = a.pkg.as_str();
    let fn_ = &a.func;
    let (seq, labels, declared, type_sets, stable) =
        (&a.seq, &a.labels, &a.declared, &a.type_sets, &a.stable);
    let func_dir = keytree::lib_func(pkg, &fn_.sig.name);
    let opcode = if pkg.is_empty() {
        fn_.sig.name.clone()
    } else {
        format!("{pkg}{}{}", keytree::MEMBER_SEP, fn_.sig.name)
    };
    let lib_key = keytree::rwir(&opcode);
    if kvkind::kind(&kv.get_one(&lib_key), &kv.langtype_of(&lib_key)) == kvkind::KIND_DEF_RWIR {
        return Err(format!(
            "{opcode}: rwir declaration and rwfunc body both define it; one opcode, one definition"
        ));
    }

    check_call_names(kv, pkg, seq, local_funcs)?;

    let mut param_coord: HashMap<String, String> = HashMap::new();
    for (i, p) in fn_.sig.params.iter().enumerate() {
        param_coord.insert(p.name.clone(), format!("*[0,-{}]", i + 1));
    }
    for (i, r) in fn_.sig.returns.iter().enumerate() {
        param_coord.insert(r.name.clone(), format!("*[0,{}]", i + 1));
    }

    /* 源级类型表：复合写目标（`(*r)`、`x·y`）与写槽兜底都按**源级**类型解析。
     * 私有结构 + 派生函数，别在调用点直接拿 entries。
     *
     * 再把 `stable` 补进去（**只补空缺**）：`resolve_slot_type` 解析复合槽名
     * （`_6·left`、`(*p)·x`）时只查这一张表，查不到就整条链没有类型。而 `_6`
     * 这类 lowering 临时槽的类型恰恰只登记在 `stable` 里——不补，复合槽与普通槽
     * 就成了两个查询面，正是 `04-tree` 那个 `_6·left` 的成因。 */
    let source_langtypes = lower::SourceLangtypes::new(&fn_.sig);
    let mut declared_source = source_langtypes.merged_under(declared);
    for (name, kind) in stable {
        if !kind.is_empty() && !kind.contains('|') {
            declared_source.entry(name.clone()).or_insert_with(|| kind.clone());
        }
    }

    let ctime_key = format!("{func_dir}/‥ctime");
    let ctime = next_ctime(read_ctime(kv, &ctime_key));

    let nr = fn_.sig.num_reads();
    let nw = fn_.sig.num_writes();
    let param_types: Vec<String> = fn_.sig.langtype_list();
    // 形参/写参的**声明类型**：容器字面量写给带类型标注的形参时（`{…} -> m`，m:[]char/utf8·int64），
    // 写槽须承载该 map langtype——否则 runtime 无从得知容器值 langtype（签名即声明处，见 [[map容器]]）。
    let mut param_langtype: HashMap<String, String> = HashMap::new();
    for (i, p) in fn_.sig.params.iter().enumerate() {
        param_langtype.insert(p.name.clone(), param_types[i].clone());
    }
    for (i, r) in fn_.sig.returns.iter().enumerate() {
        param_langtype.insert(r.name.clone(), param_types[nr as usize + i].clone());
    }

    let mut pairs: Vec<(String, String, Vec<u8>)> = Vec::new();
    pairs.push((
        format!("{func_dir}/"),
        kvkind::KIND_RWFUNC.to_string(),
        kvkind::new_rwfunc_dir()?,
    ));
    // Write `‥ctime` last (after all instructions and labels) so runtimes never rebuild from a half-written segment.
    pairs.push((
        format!("{func_dir}/[0,0]"),
        kvkind::KIND_RWFUNC.to_string(),
        kvkind::new_rwfunc_anchor(nr, nw, fn_.sig.dynamic())?,
    ));
    pairs.push((
        keytree::lib_src(pkg, &fn_.sig.name),
        "char/utf8".to_string(),
        ffi::new_char_byte(fn_.full_text().as_bytes())?,
    ));
    // 参数定义键 funcDir.[0,±k]（点后缀，与坐标斜杠键 /[0,±k] 区分）：langtype=def langtype，
    // body=名字\x00类型串。坐标斜杠键 [0,±k] 留给 runtime call 期写实参地址 Ptr；点后缀键
    // 不同名、不触发 extindex 写保护。函数体形参引用已替换为 *[0,±k] 显式解引用。
    for (i, p) in fn_.sig.params.iter().enumerate() {
        pairs.push((
            format!("{func_dir}.[0,-{}]", i + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_param(&p.name, &param_types[i])?,
        ));
    }
    for (i, r) in fn_.sig.returns.iter().enumerate() {
        pairs.push((
            format!("{func_dir}.[0,{}]", i + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_param(&r.name, &param_types[nr as usize + i])?,
        ));
    }

    // Incrementally replace only this function's subtree without disturbing sibling `/lib` entries.
    let werr = |e: String| format!("kvspace write {func_dir}: {e}");
    kv.del_tree(&func_dir).map_err(werr)?;
    kv.mkindex(&format!("{func_dir}/")).map_err(werr)?;
    kv.set(&pairs).map_err(werr)?;

    for (i, inst) in seq.iter().enumerate() {
        write_linear_inst(
            kv,
            &func_dir,
            (i as i32) + 1,
            inst,
            labels,
            declared,
            &declared_source,
            type_sets,
            structs,
            &param_coord,
            &param_langtype,
            &source_langtypes,
            stable,
            local_funcs,
            sigs,
        )
        .map_err(werr)?;
    }
    if std::env::var("KVLANG_LAYOUT_STATS").is_ok() {
        let (mut concrete, mut union, mut unknown) = (0usize, 0usize, 0usize);
        // 每条 op 观测到的操作数类型集合（union 成员表校准输入）。
        let mut per_op: std::collections::BTreeMap<
            String,
            (std::collections::BTreeSet<String>, std::collections::BTreeSet<String>),
        > = std::collections::BTreeMap::new();
        let mut record = |op: &str, ty: String, write: bool| {
            let members = lower::type_members(&ty);
            if members.is_empty() {
                unknown += 1;
                return;
            }
            if members.len() > 1 {
                union += 1;
            } else {
                concrete += 1;
            }
            let slot = per_op.entry(op.to_string()).or_default();
            let set = if write { &mut slot.1 } else { &mut slot.0 };
            set.insert(ty);
        };
        for inst in seq {
            let (op, reads) = inst.flat();
            if op.is_empty() {
                continue;
            }
            for r in &reads {
                let mut ty = read_slot_type(
                    r,
                    &param_langtype,
                    &source_langtypes,
                    declared,
                    &declared_source,
                    type_sets,
                    structs,
                    stable,
                );
                if ty.is_empty() {
                    ty = lower::literal_slot_type(r);
                }
                record(&op, ty, false);
            }
            for (j, w) in inst.writes.iter().enumerate() {
                record(
                    &op,
                    write_slot_type(inst, j, w, &param_langtype, &source_langtypes, declared, &declared_source, type_sets, structs, stable),
                    true,
                );
            }
        }
        eprintln!(
            "layout-stats: {opcode}: operands={} concrete={concrete} union={union} unknown={unknown}",
            concrete + union + unknown
        );
        for (op, (reads, writes)) in &per_op {
            // `;` 分隔：类型串本身可含 `,`（多维 dims）和 `|`（union）。
            let join = |set: &std::collections::BTreeSet<String>| {
                set.iter().cloned().collect::<Vec<_>>().join(";")
            };
            eprintln!(
                "layout-stats-op: {opcode}: {op} reads=[{}] writes=[{}]",
                join(reads),
                join(writes)
            );
        }
    }
    if !labels.is_empty() {
        kv.mkindex(&keytree::lib_labels_dir(pkg, &fn_.sig.name))
            .map_err(werr)?;
        let mut lpairs: Vec<(String, String, Vec<u8>)> = Vec::with_capacity(labels.len());
        for (label, irseq) in labels {
            // `‥labels/<label>` stores an `int64` irseq (its leaf name has no `‥`, so `printlib` needs `"int64"` langtype).
            lpairs.push((
                keytree::lib_label(pkg, &fn_.sig.name, label),
                "int64".to_string(),
                ffi::new_int64(*irseq as i64)?,
            ));
        }
        kv.set(&lpairs).map_err(werr)?;
    }
    // Commit `‥ctime` last once all instruction and label slots are persisted.
    kv.set(&[(ctime_key, String::new(), ffi::new_int64(ctime)?)])
        .map_err(werr)?;
    Ok(())
}

/// Store field types under the prototype and defaults under member keys.
pub fn write_struct_decl(kv: &mut Kv, decl: &StructDecl) -> Result<(), String> {
    let mut name = decl.name.clone();
    if !decl.pkg.is_empty() {
        name = format!("{}{}{name}", decl.pkg, keytree::MEMBER_SEP);
    }
    let base = keytree::rwir(&name);
    let mut pairs: Vec<(String, String, Vec<u8>)> = Vec::new();
    pairs.push((
        base.clone(),
        kvkind::KIND_DEF_STRUCT.to_string(),
        kvkind::new_struct()?,
    ));
    for fld in &decl.fields {
        pairs.push((
            format!("{base}/{}", fld.name),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_langtype(&fld.ty)?,
        ));
        // *T 字段的默认值是空指针 None——不落键。None 在 kvspace 即"无值"（键不存在），
        // 故原型不带该字段、实例读回 None（见 [[ptr]]）；不造"空 Ptr"这第二种空值表示。
        if fld.ty.starts_with('*') {
            continue;
        }
        pairs.push((
            keytree::member(&base, &fld.name),
            fld.ty.clone(),
            field_default(&fld.ty, fld.default.as_ref())?,
        ));
    }
    let werr = |e: String| format!("kvspace write {base}: {e}");
    kv.del_tree(&base).map_err(werr)?;
    kv.set(&pairs).map_err(werr)
}

/// 字段默认值 XValue：head kind = 字段类型，body = 默认字面量（未给则零值）。
/// 标量+char 直接编码；带 dims / structref 仅记录类型（空 body），嵌套 struct 待定。
/// Representability is validated earlier by `lower::check_field_default`; this only encodes.
fn field_default(ty: &str, default: Option<&Expr>) -> Result<Vec<u8>, String> {
    // *T 指针字段不在此列——write_struct_decl 已按"空指针 = None = 不落键"跳过。
    let (dims, base) = kvkind::parse_langtype(ty);
    let s = default.map(|e| e.val.clone()).unwrap_or_default();
    if base.starts_with("char/") {
        return ffi::new_char(&base, &s);
    }
    if !dims.is_empty() {
        return ffi::xvalue_to_wire(&base, &[], dims.iter().product());
    }
    // Allow integral float literals (e.g. `x:int64=3.0`) already validated by `check_literal`.
    let i = || {
        s.parse::<i128>()
            .ok()
            .or_else(|| s.parse::<f64>().ok().map(|f| f as i128))
            .unwrap_or(0)
    };
    let f = || s.parse::<f64>().unwrap_or(0.0);
    match base.as_str() {
        "bool" => ffi::new_bool(s == "true"),
        "int8" => ffi::xvalue_to_wire("int8", &(i() as i8).to_le_bytes(), 1),
        "int16" => ffi::xvalue_to_wire("int16", &(i() as i16).to_le_bytes(), 1),
        "int32" => ffi::xvalue_to_wire("int32", &(i() as i32).to_le_bytes(), 1),
        "int64" => ffi::new_int64(i() as i64),
        "uint8" => ffi::xvalue_to_wire("uint8", &[i() as u8], 1),
        "uint16" => ffi::xvalue_to_wire("uint16", &(i() as u16).to_le_bytes(), 1),
        "uint32" => ffi::xvalue_to_wire("uint32", &(i() as u32).to_le_bytes(), 1),
        "uint64" => ffi::xvalue_to_wire("uint64", &(i() as u64).to_le_bytes(), 1),
        "float32" => ffi::xvalue_to_wire("float32", &(f() as f32).to_le_bytes(), 1),
        "float64" => ffi::new_float64(f()),
        _ => ffi::xvalue_to_wire(&base, &[], 1),
    }
}

/// 写用户声明的 rwir（无体）到 /lib/<opcode>（kind=def rwir）。
pub fn write_rwir_decl(kv: &mut Kv, decl: &RwirDecl) -> Result<(), String> {
    let mut opcode = decl.sig.name.clone();
    if !decl.pkg.is_empty() {
        opcode = format!("{}{}{opcode}", decl.pkg, keytree::MEMBER_SEP);
    }
    let anchor = format!("{}/[0,0]", keytree::lib_func("", &opcode));
    if kvkind::kind(&kv.get_one(&anchor), &kv.langtype_of(&anchor)) == kvkind::KIND_RWFUNC {
        return Err(format!(
            "{opcode}: rwir declaration and rwfunc body both define it; one opcode, one definition"
        ));
    }
    let nr = decl.sig.num_reads();
    let nw = decl.sig.num_writes();
    let param_types = decl.sig.langtype_list();
    let base = keytree::rwir(&opcode);
    // 路由头：仅计数头（无参数载荷）；各参数类型落 [0,x] 签名行槽（def langtype）。
    let mut pairs: Vec<(String, String, Vec<u8>)> = vec![(
        base.clone(),
        kvkind::KIND_DEF_RWIR.to_string(),
        kvkind::new_defrwir(nr, nw, decl.sig.dynamic())?,
    )];
    for i in 0..nr as usize {
        pairs.push((
            format!("{base}/[0,-{}]", i + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_langtype(&param_types[i])?,
        ));
    }
    for i in 0..nw as usize {
        pairs.push((
            format!("{base}/[0,{}]", i + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_langtype(&param_types[nr as usize + i])?,
        ));
    }
    kv.set(&pairs)
        .map_err(|e| format!("kvspace write {base}: {e}"))
}

/// Flatten body into seq; ScopeStmt records label → irseq (1-based; [0,0] is the signature).
/// Returns `Err` (never panics) on block-structure violations such as duplicate labels or unterminated blocks.
fn collect_insts(
    body: &[Stmt],
    seq: &mut Vec<Instruction>,
    labels: &mut HashMap<String, i32>,
) -> Result<(), String> {
    for st in body {
        match st {
            Stmt::Instruction(s) => seq.push(s.clone()),
            Stmt::Scope(s) => {
                if s.label.is_empty() {
                    return Err("block with empty label".to_string());
                }
                if labels.contains_key(&s.label) {
                    return Err(format!(
                        "duplicate block label {:?} (labels must be unique per function; `_<name>_<n>` names are reserved for lowered control flow)",
                        s.label
                    ));
                }
                let start = seq.len() as i32 + 1;
                if seq.last().is_some_and(|last| !inst_is_terminator(last)) {
                    return Err(format!(
                        "control falls through into block label {:?} (previous instruction is not return/goto/br)",
                        s.label
                    ));
                }
                labels.insert(s.label.clone(), start);
                collect_insts(&s.body, seq, labels)?;
                /* 只有**合成**块必须自己跳出去：它的入口是跳转进来的（if/while
                 * 分支、循环体），不跳就无法到达汇合点。用户具名块按线性展平，
                 * 后面的语句就是续接——停下即衔接，不需要也不该补跳转。 */
                let terminated = (seq.len() as i32) >= start
                    && seq.last().is_some_and(inst_is_terminator);
                if s.synthetic && !terminated {
                    return Err(format!(
                        "synthetic block {:?} does not end with return/goto/br",
                        s.label
                    ));
                }
            }
            _ => {
                return Err(format!(
                    "unexpected statement after lowering: {}",
                    st.first_line()
                ))
            }
        }
    }
    Ok(())
}

/// Verify `goto`/`br` arity and target labels after linearization so emission cannot fail.
fn check_jumps(seq: &[Instruction], labels: &HashMap<String, i32>) -> Result<(), String> {
    for inst in seq {
        let (op, reads) = inst.flat();
        let label_reads: &[usize] = match op.as_str() {
            "goto" if reads.len() == 1 => &[0],
            "br" if reads.len() == 3 => &[1, 2],
            "goto" | "br" => {
                return Err(format!(
                    "{op} expects {} read(s), got {}",
                    if op == "goto" { 1 } else { 3 },
                    reads.len()
                ))
            }
            _ => continue,
        };
        for &j in label_reads {
            resolve_label(labels, &reads[j], &op)?;
        }
    }
    Ok(())
}

/// wire 形态：**不含** `*`/`@` 与 `...`（spec 04-layout语义/03-函数:17）。
/// 与 `SourceLangtypes::langtype_wire` 同一条规则，这里供「从任意类型串转 wire」用，
/// 两处必须一致——它们是同一条规则的两个入口，不是两套规则。
fn to_wire(ty: &str) -> String {
    ty.trim_end_matches("...")
        .trim_start_matches(['*', '@'])
        .to_string()
}

/// 操作数槽的静态类型：**可证明的具体 kind** → 形参/返回声明 → 源码声明 →
/// 有限类型集合（union）。任一来源都没有 → 空串（未知；由 KVLANG_LAYOUT_STATS 计数）。
///
/// 具体优先是 encoder 收敛的前提：opcode 不再携带 `<kind>·` 前缀后，读取层只能从
/// 槽 langtype 拿到类型；声明的 def langtype 可能是并集（`int64|float64`），并集
/// 无法收敛成单一具体类型。故凡赋值来源能证明唯一具体 kind，就写该 kind。
fn slot_static_type(
    name: &str,
    param_langtype: &HashMap<String, String>,
    source_langtypes: &lower::SourceLangtypes,
    declared: &HashMap<String, String>,
    declared_source: &HashMap<String, String>,
    type_sets: &HashMap<String, Vec<String>>,
    structs: &lower::StructTable,
    stable: &HashMap<String, String>,
) -> String {
    // 两张表的键与值形态不同，不能混用：
    // - `param_langtype`：键 = 形参名，值 = **wire langtype**（已剥 `*`/`@`）；
    // - `declared`：键 = 形参名，值 = **变量类型**（`pointer_type()`，带前缀）。
    //
    // `*p` 的槽类型是解引用**一层**的结果：从 `declared` 取值剥一层最直接；
    // 从 `param_langtype` 取值时那一层已经在入表时剥掉了，不能再剥。
    // 复合槽名（`(*r)`、`x·y`）不在这四张表里按原名出现：交给**同一个**解析器
    // `lower::resolve_slot_type`（不在这里再写一份）。它按源级类型解析，返回的是
    // **值**类型；读槽要的是 wire 形态，故过 `to_wire`。
    if !lower::slot_is_plain_name(name) {
        /* 复合槽名的基座直接取自本函数传进来的两张表：`declared_source` 是源级
         * （形参 `*T` 保留），`declared` 是值级（形参已剥）。用两份是**故意的**：
         * `_6·left` 的 `_6` 只登记在 `stable`（已并进 `declared_source`），而形参
         * 名只在值级表里带对了形态。两表同源，这里只是各自给 `resolve_slot_type`
         * 它能解析的那一种。 */
        let resolved = lower::resolve_slot_type(name, declared_source, structs, declared_source)
            .or_else(|| lower::resolve_slot_type(name, declared, structs, declared));
        if let Some(resolved) = resolved {
            let wire = to_wire(&resolved);
            if !wire.is_empty() {
                return narrow_union(wire, name, stable);
            }
        }
    }
    let lookup_name = name.strip_prefix('*').unwrap_or(name);
    let is_deref = lookup_name.len() != name.len();
    let declared_any = declared.get(lookup_name).filter(|t| !t.is_empty());
    if let Some(t) = declared_any {
        let value = if is_deref {
            lower::deref_value_type(t)
        } else {
            t.as_str()
        };
        if !value.is_empty() {
            return narrow_union(value.to_string(), lookup_name, stable);
        }
    }
    // 形参类型**经表的 wire 派生**取（表是唯一存储形态；`param_langtype` 只作兜底，
    // 两者同源：wire = 源级去 `*`/`@`/`...`）。
    if let Some(t) = source_langtypes.langtype_wire(lookup_name) {
        if !t.is_empty() {
            return narrow_union(t, lookup_name, stable);
        }
    }
    if let Some(t) = param_langtype.get(lookup_name).filter(|t| !t.is_empty()) {
        return narrow_union(t.clone(), lookup_name, stable);
    }
    /* 可证明的具体 kind：这里也是 lowering 临时槽（`_N`）写槽类型的唯一回填处。
     * 少了这一条，读 `_N` 的指令拿到空类型，编码器报「不是单一具体类型」。 */
    if let Some(t) = stable.get(lookup_name).filter(|t| !t.is_empty()) {
        return t.clone();
    }
    lower::type_set_slot_type(type_sets, lookup_name).unwrap_or_default()
}

fn read_slot_type(
    name: &str,
    param_langtype: &HashMap<String, String>,
    source_langtypes: &lower::SourceLangtypes,
    declared: &HashMap<String, String>,
    declared_source: &HashMap<String, String>,
    type_sets: &HashMap<String, Vec<String>>,
    structs: &lower::StructTable,
    stable: &HashMap<String, String>,
) -> String {
    slot_static_type(
        name,
        param_langtype,
        source_langtypes,
        declared,
        declared_source,
        type_sets,
        structs,
        stable,
    )
}

/// 给线性化后的指令补写槽类型（lower 之后、写 kvspace 之前）。
///
/// 为什么需要这一趟：`lower_func` 会把复合表达式展开成临时槽（`_1`、`_2`…），
/// 这些槽既不在 `declared` 也不在类型集合里，于是 body 只有名字、没有 `\0类型`，
/// 编码器报「local '_N' is written with a non-concrete langtype」——类型缺失被
/// 报成编码器缺陷。
///
/// **三类缺口共用这一个入口**，不各写一遍：
///   A. 调用结果：`f(x)` 的写槽类型 = callee 的返回类型（`sigs`）；
///   B. mapexpr 成员读：`m·k` 的结果类型 = langtype 里 `·` 之后那段；
///   D. 指针解引用：`*p` 的结果类型 = `declared[p]` 去掉前导 `*`。
///
/// 空 kindexpr（`rwfunc f(x:)`）**不在此补**：spec 说它合法（「不约束」），
/// 替它编一个具体类型就是发明语义。那条由 runtimec 明确拒绝（见 BOARD）。
///
/// **读侧与写侧共用一张表**：推导结果并进 `stable`，写 `stable` 与后来读同一槽
/// 的指令查的都是它。见函数体内的并入规则。
fn annotate_slot_types(
    seq: &mut [Instruction],
    declared: &HashMap<String, String>,
    type_sets: &HashMap<String, Vec<String>>,
    stable: &mut HashMap<String, String>,
    sigs: &lower::SigTable,
    structs: &lower::StructTable,
) {
    /*
     * lowering 产生的临时槽（`_N`）与**隐式局部**（`(*p)·x -> t` 的 `t`）不在
     * `declared`/`type_sets`/`stable` 任何一张表里。它们的写槽类型由本函数推导并
     * 写在 `instruction.write_types` 上；读它们的指令必须看到**同一个**结果。
     *
     * 所以推导结果直接并进 `stable`——读侧的最后一个来源，全函数唯一的一份。
     * 不另建 `derived` 这类第二张表：写侧与读侧各查一张表，就是「同一事实两个
     * 来源」，改一处漏一处（先例：写侧早就在 `write_func` 里合并，读侧却查不到）。
     *
     * 顺序扫描保证：后出现的指令查到的 `stable` 已含前面指令推出的类型。
     * 本轮内全程同一张表，不存在「一半查合并前、一半查合并后」。
     *
     * **并入规则（硬约束）**：
     *   - 只补空缺：`declared` / `type_sets` / `stable` 已有的名字一律以原表为准；
     *   - 只收已推出的**具体**类型：空串与并集（含 `|`）一律不收——`stable` 还被
     *     `narrow_union` 当作「可证明类型」用，塞进并集就毁掉那个语义。
     *
     * **「已有」必须按 `type_sets` 的成员数判，不能按 `contains_key`**：
     * `type_sets` 收的是「该名字所有写位置的类型**集合**」，临时槽往往只有一条
     * `*p` 来源，于是 `{"…"}` 是**单元素集合**——那已经是可证明的具体类型，
     * 只是 `stable` 不认（它只收数值 kind）。按 `contains_key` 判会把这类名字
     * 当成「别处已有」而跳过，随后 `slot_static_type` 查 `type_sets` 又只能拼出
     * union 串（单元素也可能带括号），最终读槽拿到空类型。`04-tree` 的 `_6·left`
     * 与 `083` 的 `_5·next` 都是这条。
     */
    /*
     * 本轮扫描直接读写 `stable`，不再另建 `derived`：曾经这里是
     * `let mut derived = HashMap::new()`，只服务本轮扫描；写侧 `write_func` 又
     * 自带一份 `stable_with_temporaries` 做同样的事。两张表算的是同一件事
     * （「这个临时槽的类型是什么」），却是两个查询面。
     */
    for inst in seq.iter_mut() {
        if inst.writes.is_empty() {
            continue;
        }
        let Some(expr) = inst.expr.as_ref() else {
            continue;
        };
        /* 叶子表达式（`s -> t`、`*p -> t`）也要走一遍：拷贝型写槽的类型就是**源**
         * 的类型。**跳过叶子**曾经让「先拷贝、再取成员」这条链整条断掉——
         * `derived_base` 探针（`s:N={}; s -> t; t·val`）与 `walk_lib.kv` 都是这条：
         * `t` 从没进过 `stable`，于是 `t·val` 这个复合读槽找不到基座。
         *
         * 源的类型用**同一个解析器**取（`resolve_slot_type`），不在这里另写一份：
         * 它同时覆盖裸名、`*p` 解引用、`(*p)·seg` 与坐标段。 */
        let ty = if expr.is_leaf() {
            let resolved = lower::resolve_slot_type(&expr.val, declared, structs, declared)
                .or_else(|| lower::resolve_slot_type(&expr.val, stable, structs, stable));
            resolved.map(|t| to_wire(&t)).unwrap_or_default()
        } else {
            expr_slot_type(expr, declared, sigs, stable, structs).unwrap_or_default()
        };
        let ty = (!ty.is_empty()).then_some(ty);
        if let Some(ty) = ty.filter(|value| !value.is_empty()) {
            if inst.write_types.len() < inst.writes.len() {
                inst.write_types.resize(inst.writes.len(), String::new());
            }
            if inst.write_types[0].is_empty() {
                inst.write_types[0] = ty;
            }
        }
        let learned: Vec<(String, String)> = inst
            .writes
            .iter()
            .zip(&inst.write_types)
            .filter(|(_, write_type)| !write_type.is_empty() && !write_type.contains('|'))
            .filter(|(write, _)| {
                !declared.contains_key(*write)
                    && !type_sets
                        .get(*write)
                        .is_some_and(|members| members.len() > 1)
                    && !stable.contains_key(*write)
            })
            .map(|(write, write_type)| (write.clone(), write_type.clone()))
            .collect();
        for (write, write_type) in learned {
            stable.entry(write).or_insert(write_type);
        }
    }
}

/// 一个写槽表达式的结果类型；None = 不归本入口管（由常规链路处理或本就未知）。
fn expr_slot_type(
    expr: &ast::Expr,
    declared: &HashMap<String, String>,
    sigs: &lower::SigTable,
    derived: &HashMap<String, String>,
    structs: &lower::StructTable,
) -> Option<String> {
    /* D：指针解引用 `*p`；B：成员读 `base·seg…`。
     * 两者都可能以**复合表达式**为基座（`(*p)·x`），所以走同一套递归取型。 */
    if expr.op == ast::DEREF_OP || expr.op == ast::MEMBER_READ_OP {
        return value_type_of(expr, declared, derived, structs);
    }
    /* `compactarray·fill` 的**首个形参就是写目标的 langtype**——这是它的**签名**，
     * 不是「第一个字面量」这种语法巧合（spec `array-fill-write-param`：它填的是
     * 一段定宽数组，类型由调用方给出）。
     *
     * 判据挂在签名上：**先查 `sigs`**（`compactarray·fill` 若有安装签名就以它为准，
     * 形状为 `fill(*[]char/utf8 $t) -> …`），签名不可得时才按「首参是 langtype
     * 文本」取——因为这一行的类型本来就来自调用方写下的那串 langtype，而它必然
     * 是个字符串字面量。
     *
     * 明确记一笔：这里**不使用**「带不带引号」作为语义判据。今晚已有两次栽在
     * 「用字符形态代替语义判断」（`starts_with('[')` 误判 `[]char`、
     * `w.find('[')` 把 `a·[0]` 截成 `a·`）。
     *
     * 这一支缺失时写槽 `empty` 没有类型——本行是 install signature 行、
     * `sig_lookup` 查不到，落到 `call_result_type` 而那里也没有 fill。
     */
    if expr.op == "compactarray·fill" {
        if let Some(signature) = lower::sig_lookup(sigs, &expr.op) {
            if let Some(first) = signature.params.first() {
                if !first.ty.is_empty() && !first.ty.contains('|') {
                    return Some(first.ty.clone());
                }
            }
        }
        return expr.args.first().and_then(|argument| {
            let text = argument.val.trim();
            (!text.is_empty()).then(|| text.to_string())
        });
    }
    /* **类型转换算子的结果类型就是算子名本身**——这是**规则**，不是特例：
     * `int64(x)` → `int64`、`char/utf32(s)` → `char/utf32`，因为算子名写的就是
     * 目标类型。
     *
     * `is_cast_op` 只列了数值 kind，是既有缺陷：`char/utf8` / `char/utf32` /
     * `char/ascii` 三种字符转换算子**从来没进过这一支**（`infer_op_type` 里
     * 对应的那条对它们一直是死代码）。所以这里把判据扩到 `char/` 系列，
     * 与 `infer_op_type` 保持同一答案。 */
    if lower::is_cast_op(&expr.op) || expr.op.starts_with("char/") {
        return Some(expr.op.clone());
    }
    /* A：调用 `f(…)` → callee 的返回类型。 */
    /* 与 `annotate_returns_inst` 共用同一查找：先文件签名，再 native 表。
     * 只查 `sigs` 会漏掉 `time·now` 这类**不落 /lib** 的能力——它们在嵌套调用
     * 被 flatten 成临时槽之后才需要类型（命名局部在 lowering 前已被注解）。
     *
     * 查不到签名时**再**从实参推：`kvspace·listat` 这类「返回类型是实参的函数」
     * 的算子进不了 native 表（那张表只放无前因的常量类型），只能在这里推。
     * 顺序固定「签名优先」：有签名的算子以签名为准，实参推导只做兜底。 */
    let Some(sig) = lower::sig_lookup(sigs, &expr.op) else {
        let argument_types: Vec<Option<String>> = expr
            .args
            .iter()
            .map(|argument| value_type_of(argument, declared, derived, structs))
            .collect();
        let argument_literals: Vec<Option<String>> = expr
            .args
            .iter()
            .map(|argument| {
                (argument.is_leaf() && argument.quote != 0 && !argument.val.is_empty())
                    .then(|| argument.val.clone())
            })
            .collect();
        return lower::call_result_type(&expr.op, &argument_types, &argument_literals);
    };
    let first = sig.returns.first()?;
    if first.ty.is_empty() || first.ty.contains('|') {
        /* 空 kindexpr 不补；union 交给常规链路（那里已有 union 收敛规则）。 */
        return None;
    }
    Some(first.ty.clone())
}

/// 一个表达式的**值类型**：叶子查声明/已推导表；成员读与解引用递归向基座取。
///
/// 与 `lower::arg_type` 同形——那边早就按复合基座递归了（`(*p)·x` 的基座是解引用），
/// 而 `expr_slot_type` 原先只查叶子 `val`，于是 `(*p)·field` 这类读槽拿不到类型。
/// 两处逻辑必须一致：同一类表达式在写槽与读槽上得到同一个答案。
///
/// 成员链交给 `lower::member_chain_type`（mapexpr / 数组 / struct 三种基座它都认），
/// 不在这里再写一遍逐段推导——那正是「事实在表里、路径没拿到表」的第二个入口。
fn value_type_of(
    expr: &ast::Expr,
    declared: &HashMap<String, String>,
    derived: &HashMap<String, String>,
    structs: &lower::StructTable,
) -> Option<String> {
    if expr.op == ast::DEREF_OP {
        let inner = expr.args.first()?;
        let ty = value_type_of(inner, declared, derived, structs)?;
        return Some(ty.strip_prefix('*').unwrap_or(&ty).to_string());
    }
    if expr.op == ast::MEMBER_READ_OP {
        let base = expr.args.first()?;
        let base_type = value_type_of(base, declared, derived, structs)?;
        let tail: Vec<&str> = expr.args.iter().skip(1).map(|a| a.val.as_str()).collect();
        let member = tail.join(keytree::MEMBER_SEP);
        return lower::member_chain_type(&base_type, &member, structs)
            /* 坐标段 `base·[i]` 的段文本是 `[i]`：`member_chain_type` 按「成员名」查
             * struct 字段自然查不到，但它对**容器**有现成规则——`container_value`
             * 取 `·` 之后的值类型，与键是不是坐标段无关。这里补的就是那一条：
             * `m:[int64]·int64` 的 `m·[0]` 与 `m·0` 是同一个值类型。 */
            .or_else(|| {
                tail.iter()
                    .all(|segment| segment.starts_with('[') && segment.ends_with(']'))
                    .then(|| lower::container_value(&base_type))
                    .flatten()
            });
    }
    if expr.val.is_empty() {
        return None;
    }
    declared
        .get(&expr.val)
        .or_else(|| derived.get(&expr.val))
        .cloned()
}

/// 成员读 `base·field` 的结果类型：基座是 structref 时，字段类型在 struct 原型里。
///
/// 为什么不能靠常规链路：`a·b -> _1` 的写目标是 lower 展开的临时槽，它既不在
/// `declared` 也不在类型集合里，于是 body 只有名字、没有 `\0类型`，编码器报
/// 「local '_1' is written with a non-concrete langtype」——类型缺失被报成
/// 编码器缺陷。
///
/// 类型事实**已经在指令里**：基座的名字在 `declared`（或形参表）里有 structref，
/// 字段名是第二个读参。从已有事实取，不另建表。
fn member_read_type(
    s: &Instruction,
    declared: &HashMap<String, String>,
    structs: &lower::StructTable,
    stable: &HashMap<String, String>,
) -> Option<String> {
    let e = s.expr.as_ref()?;
    if e.is_leaf() || e.op != ast::MEMBER_READ_OP {
        return None;
    }
    let base = e.args.first()?;
    /* 基座可能是 lowering 临时槽（`(*node)·left` 的 `_8`）：先查声明表，再查
     * 可证明类型表（write_func 已把临时槽的写槽类型并进去）。 */
    let base_type = declared
        .get(&base.val)
        .or_else(|| declared.get(base.val.trim_start_matches('*')))
        .or_else(|| stable.get(&base.val))
        .or_else(|| stable.get(base.val.trim_start_matches('*')))?;
    let field = e.args.get(1)?;
    let name = base_type
        .trim_start_matches('*')
        .strip_prefix("/lib/")
        .unwrap_or(base_type.trim_start_matches('*'));
    structs
        .get(name)?
        .fields
        .iter()
        .find(|f| f.name == field.val)
        .map(|f| f.ty.clone())
        .filter(|t| !t.is_empty())
}

/// 写槽类型：本条注解（含 call 返回）→ 形参/返回 → 声明可解析类型 → 类型集合。
fn write_slot_type(
    s: &Instruction,
    j: usize,
    orig: &str,
    param_langtype: &HashMap<String, String>,
    source_langtypes: &lower::SourceLangtypes,
    declared: &HashMap<String, String>,
    declared_source: &HashMap<String, String>,
    type_sets: &HashMap<String, Vec<String>>,
    structs: &lower::StructTable,
    stable: &HashMap<String, String>,
) -> String {
    if let Some(t) = s.write_types.get(j).filter(|t| !t.is_empty()) {
        return t.clone();
    }
    if let Some(t) = member_read_type(s, declared, structs, stable) {
        return t;
    }
    if let Some(t) = param_langtype.get(orig).filter(|t| !t.is_empty()) {
        return t.clone();
    }
    // `declared_source` = 写槽声明 ∪ **源级**形参类型：复合写目标（`(*r)`、`x·y`）
    // 要按源级类型解析。用普通 `declared` 会漏掉形参（它只登记写槽），
    // 用 `param_langtype` 又会混进 wire 形态（`*` 已被剥）。
    if let Some(t) = lower::resolve_slot_type(orig, declared_source, structs, declared_source) {
        return narrow_union(t, orig, stable);
    }
    slot_static_type(
        orig,
        param_langtype,
        source_langtypes,
        declared,
        declared_source,
        type_sets,
        structs,
        stable,
    )
}

/// 声明的 def langtype 可能是并集；若赋值来源能证明唯一具体 kind，并且该 kind
/// 是并集成员，就把它当成槽类型（encoder 只能收敛到单一具体类型）。
fn narrow_union(ty: String, name: &str, stable: &HashMap<String, String>) -> String {
    if !ty.contains('|') {
        return ty;
    }
    match stable.get(name).filter(|k| !k.contains('|')) {
        Some(k) if lower::type_members(&ty).contains(&k.as_str()) => k.clone(),
        _ => ty,
    }
}

#[allow(clippy::too_many_arguments)]
/// 槽文本 → 形参坐标（`*[0,±k]`）。
///
/// 形参名在指令槽里有两种写法：裸名（`println(s)`）与前导 `*`（`*a + 1`、`-> *r`）。
/// `param_coord` 的键是裸名，所以先按原文查，未命中再剥**一层**前导 `*` 查；两者语义
/// 相同——body 的 `*a` 就是「解引用形参槽」，正是 `*[0,-1]`。剥完仍不在表里就回落原文：
/// 非形参的指针表达式（`*[0,-1]` 自身、`*p` 局部指针）不能因为形似而被改写。
fn param_slot<'a>(params: &'a HashMap<String, String>, text: &'a str) -> &'a str {
    if let Some(value) = params.get(text) {
        return value.as_str();
    }
    if let Some(bare) = text.strip_prefix('*') {
        if let Some(value) = params.get(bare) {
            return value.as_str();
        }
    }
    text
}

fn write_linear_inst(
    kv: &mut Kv,
    prefix: &str,
    n: i32,
    s: &Instruction,
    labels: &HashMap<String, i32>,
    declared: &HashMap<String, String>,
    declared_source: &HashMap<String, String>,
    type_sets: &HashMap<String, Vec<String>>,
    structs: &lower::StructTable,
    params: &HashMap<String, String>,
    param_langtype: &HashMap<String, String>,
    source_langtypes: &lower::SourceLangtypes,
    stable: &HashMap<String, String>,
    local_funcs: &HashSet<String>,
    sigs: &lower::SigTable,
) -> Result<(), String> {
    let (opcode, mut reads) = s.flat();
    // Already validated by `check_jumps`; still return `Result` to avoid panic paths.
    match opcode.as_str() {
        "goto" if reads.len() == 1 => {
            reads[0] = resolve_label(labels, &reads[0], "goto")?.to_string();
        }
        "br" if reads.len() == 3 => {
            reads[1] = resolve_label(labels, &reads[1], "br")?.to_string();
            reads[2] = resolve_label(labels, &reads[2], "br")?.to_string();
        }
        "goto" | "br" => return Err(format!("{opcode}: bad arity {}", reads.len())),
        _ => {}
    }
    /*
     * `None` 操作数的槽 langtype = **该位置上下文要求的类型**。
     *
     * 为什么必须有类型：wire 仍是长度 0 的短内联头（表示是 None），但编码器要
     * 按 langtype 把操作数归进类别表才能选出算子；空 langtype 在任何算子下都选
     * 不出来。类型不在这层补，就只能在 vmkv 里从邻居猜——那是在看不到源码的层
     * 做类型推断，等于给主干长分支。
     *
     * 三种上下文（spec `01-词法/07` 与 `03-类型系统/16-ptr`）：
     *   比较：取另一侧的类型，且必须是引用（`*T`）——opcode 保持 `==`/`!=`，
     *         由编码层的特化规则按「两侧都是指针」改选到 `ptreq`/`ptreq·not`
     *         （`rwir_alias_table.def` 的两条 SPECIALIZATION 记录）。**不在 layout
     *         改 opcode**：那会在编码层之上再造一套算子选择。
     *   赋值/声明初始化：取写槽的声明类型；
     *   调用实参：取被调方形参类型。
     * 三种都给不出具体类型时在 **layout** 期报错，不许漏到编码器。
     */
    let none_index = reads.iter().position(|operand| operand == "None");
    let mut none_type: Option<String> = None;
    if let Some(index) = none_index {
        let comparison = opcode == "==" || opcode == "!=";
        let resolved = if comparison {
            let other = reads
                .get(if index == 0 { 1 } else { 0 })
                .ok_or_else(|| format!("{opcode}: None 比较缺少另一侧操作数"))?;
            let other_type = read_slot_type(
                other,
                param_langtype,
                source_langtypes,
                declared,
                declared_source,
                type_sets,
                structs,
                stable,
            );
            if other_type.is_empty() {
                return Err(format!(
                    "{opcode}: 无法判定空引用比较的类型——另一侧 `{other}` 没有具体类型"
                ));
            }
            /* 判据只有一份：`lower::is_none_comparable`（指针 `*T` 与 structref
             * `/lib/X`）。这里不能写「以 `*` 开头」——`(*node)·left` 的成员读结果
             * 是值形态的 `/lib/TNode`（`*` 是传递方式，已被剥），按 `*` 判会把
             * 一条合法的判空挡在 layout 期。 */
            if !lower::is_none_comparable(&other_type) {
                return Err(format!(
                    "{opcode}: 空引用判定的另一侧不是引用类型（`{other}` 的类型是 `{other_type}`）"
                ));
            }
            other_type
        } else if let Some(sig) = sigs.get(&opcode) {
            let parameter = sig.params.get(index).ok_or_else(|| {
                format!("{opcode}: None 实参位置 {index} 超出台数形参表")
            })?;
            if parameter.ty.is_empty() || parameter.ty.contains('|') {
                return Err(format!(
                    "{opcode}: 无法判定 None 实参的类型——形参 `{}` 没有具体类型",
                    parameter.name
                ));
            }
            parameter.ty.trim_start_matches(['*', '@']).to_string()
        } else if s.writes.len() == 1 {
            let declared_type =
                write_slot_type(s, 0, &s.writes[0], param_langtype, source_langtypes, declared, declared_source, type_sets, structs, stable);
            if declared_type.is_empty() || declared_type.contains('|') {
                return Err(format!(
                    "{} -> {}: 无法判定 None 的类型——写槽没有具体声明类型",
                    opcode, s.writes[0]
                ));
            }
            declared_type
        } else {
            return Err(format!("{opcode}: 无法判定 None 操作数的上下文类型"));
        };
        none_type = Some(resolved);
    }

    let target_char = if s.writes.len() == 1
        && !s.write_types.is_empty()
        && kvkind::is_char_kind(&s.write_types[0])
    {
        s.write_types[0].as_str()
    } else {
        ""
    };

    let mut pairs: Vec<(String, String, Vec<u8>)> =
        Vec::with_capacity(1 + reads.len() + s.writes.len());
    if !opcode.is_empty() {
        let (wire, lt) = opcode_value(
            kv,
            &opcode,
            reads.len() as i32,
            s.writes.len() as i32,
            local_funcs,
            s.operator,
        )?;
        pairs.push((format!("{prefix}/[{n},0]"), lt, wire));
    }
    for (j, r) in reads.iter().enumerate() {
        let rv = param_slot(params, r);
        // 未定型整数字面量适配（spec 03-类型系统/01 第 10 条）：算子操作数的字面量
        // 按 promotion 结果 kind 落槽（`uint64_var > 1` 的 `1` 落成 uint64）。
        if let Some((wire, lt)) = adapted_literal_slot(rv, &s.operand_kind)? {
            pairs.push((format!("{prefix}/[{n},-{}]", j + 1), lt, wire));
            continue;
        }
        if Some(j) == none_index {
            /* wire 恒是 None 头；只有槽 langtype 由上下文决定。 */
            let wire = ffi::new_none()?;
            let context_type = none_type.clone().unwrap_or_default();
            pairs.push((format!("{prefix}/[{n},-{}]", j + 1), context_type, wire));
            continue;
        }
        let ty = read_slot_type(
            r,
            param_langtype,
            source_langtypes,
            declared,
            declared_source,
            type_sets,
            structs,
            stable,
        );
        let (wire, lt) = typed_slot_value(rv, &ty, target_char)?;
        pairs.push((format!("{prefix}/[{n},-{}]", j + 1), lt, wire));
    }
    for (j, w) in s.writes.iter().enumerate() {
        let orig = w.as_str();
        let wv = param_slot(params, orig);
        let ty = write_slot_type(s, j, orig, param_langtype, source_langtypes, declared, declared_source, type_sets, structs, stable);
        let (wire, lt) = typed_slot_value(wv, &ty, "")?;
        pairs.push((format!("{prefix}/[{n},{}]", j + 1), lt, wire));
    }
    if !pairs.is_empty() {
        kv.set(&pairs)?;
    }
    Ok(())
}

fn inst_is_terminator(inst: &Instruction) -> bool {
    match &inst.expr {
        Some(e) if e.is_leaf() => e.val == "return",
        Some(e) => matches!(e.op.as_str(), "return" | "goto" | "br"),
        None => false,
    }
}

fn resolve_label(labels: &HashMap<String, i32>, name: &str, opcode: &str) -> Result<i32, String> {
    labels
        .get(name)
        .copied()
        .ok_or_else(|| format!("{opcode}: unknown label {name:?}"))
}

/// 调用名必须是「全路径去掉 `/lib`」：全局函数写 `name`，包内函数写 `包名·name`。
/// 裸名合法当且仅当 `/lib/<name>` 是已布局函数、已声明 rwir（def rwir），
/// 或该词是语言算子（算术/比较/逻辑/cast）；否则一律报错——**没有包内回退**
/// （该回退曾让包内裸名与全局函数争名空间，导致 `==`→`eq` 被解析成
/// `string·eq` 的 6976 层递归）。
fn check_call_names(
    kv: &mut Kv,
    pkg: &str,
    seq: &[Instruction],
    local_funcs: &HashSet<String>,
) -> Result<(), String> {
    for inst in seq {
        if inst.operator {
            continue;
        }
        let (op, _) = inst.flat();
        if !is_bare_identifier(&op) {
            continue;
        }
        if matches!(op.as_str(), "=" | "return" | "goto" | "br" | "call") {
            continue;
        }
        // 合法：全局函数（本文件裸定义 / 已布局）或已声明 rwir（def rwir）。
        if local_funcs.contains(&op) {
            continue;
        }
        let dir = format!("/lib/{op}/");
        if kvkind::kind(&kv.get_one(&dir), &kv.langtype_of(&dir)) == kvkind::KIND_RWFUNC {
            continue;
        }
        let decl = format!("/lib/{op}");
        if kvkind::kind(&kv.get_one(&decl), &kv.langtype_of(&decl)) == kvkind::KIND_DEF_RWIR {
            continue;
        }
        // 本包存在同名函数：裸名非法（必须写 `包名·name` 或 `/lib/包名·name`），
        // 无论它是否还是算子词（`eq` 在 `lib string` 里就是典型）。
        if !pkg.is_empty() {
            let qual = format!("{pkg}{}{op}", keytree::MEMBER_SEP);
            let qdir = format!("/lib/{qual}/");
            if local_funcs.contains(&qual)
                || kvkind::kind(&kv.get_one(&qdir), &kv.langtype_of(&qdir))
                    == kvkind::KIND_RWFUNC
            {
                return Err(undefined_call(&op));
            }
        }
        // 语言算子（含 word 形式 add/eq/…、逻辑词、cast）不是函数调用。
        if crate::builtin::num_op(&op)
            || matches!(op.as_str(), "and" | "or" | "not")
            || is_cast_name(&op)
        {
            continue;
        }
        // 其余裸名：既不是全局函数、也未声明 rwir、也不是本包函数 → 明确报错，
        // 绝不放行（`debugger` 之类未实现的内建应由提供方 def rwir 声明）。
        return Err(undefined_call(&op));
    }
    Ok(())
}

fn undefined_call(op: &str) -> String {
    format!("未定义函数 {op}：未给出被调函数的完整路径（写 `包名·函数名` 或 `/lib/包名·函数名`）")
}

fn is_cast_name(op: &str) -> bool {
    matches!(
        op,
        "int8" | "int16" | "int32" | "int64"
            | "uint8" | "uint16" | "uint32" | "uint64"
            | "float32" | "float64" | "bool"
            | "char/utf8" | "char/utf32" | "char/ascii"
    ) || op == "stringkeymap"
}

/// 裸标识符（字母/数字/下划线/非 ASCII），排除符号算子、路径、命名空间算子。
fn is_bare_identifier(s: &str) -> bool {
    !s.is_empty()
        && !s.contains('/')
        && !s.contains('.')
        && !s.contains(keytree::MEMBER_SEP)
        && s.chars().all(|c| c.is_alphanumeric() || c == '_')
}

fn opcode_value(
    kv: &mut Kv,
    opcode: &str,
    nr: i32,
    nw: i32,
    local_funcs: &HashSet<String>,
    is_operator: bool,
) -> Result<(Vec<u8>, String), String> {
    // lower 认定的算子永不是函数调用：语义名可能与同包 rwfunc 同名
    // （`==` → `eq` vs `lib string { rwfunc eq }`），不得被裸名解析抢走。
    if is_operator {
        return Ok((
            kvkind::new_rwir(nr, nw, opcode)?,
            kvkind::KIND_RWIR.to_string(),
        ));
    }
    // 裸名只有一条解析路径：`/lib/<name>`。删除「回退到 `<pkg>·<name>`」分支
    // （包内裸名与全局函数争名空间，是 6976 层递归的根源）；同包调用必须写
    // `pkg·name`（带 `·`），未定义裸名由 check_call_names 报错。
    let name = opcode.strip_prefix("/lib/").unwrap_or(opcode);
    let is_func = local_funcs.contains(name)
        || kvkind::kind(
            &kv.get_one(&format!("/lib/{name}/")),
            &kv.langtype_of(&format!("/lib/{name}/")),
        ) == kvkind::KIND_RWFUNC;
    Ok(if is_func {
        // 全局函数调用一律落绝对路径，运行期不得再做包内回退。
        let target = if opcode.starts_with("/lib/")
            || name.contains(keytree::MEMBER_SEP)
            || name.contains('/')
        {
            opcode.to_string()
        } else {
            format!("/lib/{name}")
        };
        (
            kvkind::new_rwfunc_call(nr, nw, &target)?,
            kvkind::KIND_RWFUNC.to_string(),
        )
    } else {
        (
            kvkind::new_rwir(nr, nw, opcode)?,
            kvkind::KIND_RWIR.to_string(),
        )
    })
}

/// Preserve the target type.
/// 整数字面量文本（可选负号，无小数点/指数）。
fn integer_literal_text(s: &str) -> bool {
    let b = s.as_bytes();
    let start = usize::from(b.first() == Some(&b'-'));
    let rest = &b[start..];
    !rest.is_empty() && rest.iter().all(u8::is_ascii_digit)
}

/// 无符号算子的字面量操作数：按 promotion 结果 kind 编码（范围复检，越界不改编码）。
fn adapted_literal_slot(
    text: &str,
    kind: &str,
) -> Result<Option<(Vec<u8>, String)>, String> {
    if !builtin::is_unsigned_kind(kind) || !integer_literal_text(text) {
        return Ok(None);
    }
    let Ok(v) = text.parse::<i128>() else {
        return Ok(None);
    };
    if v < 0 {
        return Ok(None);
    }
    let v = v as u128;
    let (bytes, max): (Vec<u8>, u128) = match kind {
        "uint8" => (vec![v as u8], u8::MAX as u128),
        "uint16" => ((v as u16).to_le_bytes().to_vec(), u16::MAX as u128),
        "uint32" => ((v as u32).to_le_bytes().to_vec(), u32::MAX as u128),
        "uint64" => ((v as u64).to_le_bytes().to_vec(), u64::MAX as u128),
        _ => return Ok(None),
    };
    if v > max {
        return Ok(None);
    }
    let wire = ffi::xvalue_to_wire(kind, &bytes, 1)?;
    Ok(Some((wire, kind.to_string())))
}

fn typed_slot_value(name: &str, ty: &str, target_char: &str) -> Result<(Vec<u8>, String), String> {
    if !ty.is_empty() && (name.starts_with('/') || !is_literal(name)) {
        return Ok((
            kvkind::new_typed_rwir(name, ty)?,
            kvkind::KIND_RWIR.to_string(),
        ));
    }
    slot_value(name, target_char)
}

/// 指令操作数的 wire 解析；langtype 精确匹配 wire 的 kind。
fn slot_value(val: &str, target_char: &str) -> Result<(Vec<u8>, String), String> {
    /*
     * `None` 是字面量，不是名字。它的 wire 是一条长度 0 的短内联头、langtype 空。
     *
     * 为什么不能落到下面 `new_rwir`：那会把 langtype 写成 `rwir`，于是 vmkv 按
     * 「裸名/相对指针」分类，把 `None` 登记成一个读而未写的局部变量——C5 的整条
     * 失败链就是这么来的。判据是**源码 token 是不是 None**，这一层是唯一知道
     * 它来自源码记号的地方（下方 vmkv 只能看到槽，看不到源文本）。
     *
     * 只处理读侧：写目标叫 `None` 的情形不该走到这里，也不在这里放行。
     */
    if val == "None" {
        return Ok((ffi::new_none()?, String::new()));
    }
    if !is_literal(val) {
        return Ok((
            kvkind::new_rwir(0, 0, val)?,
            kvkind::KIND_RWIR.to_string(),
        ));
    }
    let b = val.as_bytes();
    if b[0] == b'"' {
        let mut s = val;
        if !s.is_empty() && s.as_bytes()[0] == b'"' {
            s = &s[1..];
        }
        let k = if target_char.is_empty() {
            kvkind::KIND_CHAR
        } else {
            target_char
        };
        /* 字符串 langtype 带 [len] 形状段（码点数）——xv/array 下标按它解 dims。 */
        let lt = format!("[{}]{k}", s.chars().count());
        return Ok((ffi::new_char(k, s)?, lt));
    }
    if val == "true" || val == "false" {
        return Ok((ffi::new_bool(val == "true")?, "bool".to_string()));
    }
    if b[0].is_ascii_digit() || (b[0] == b'-' && val.len() > 1) {
        if val.contains('.') || val.contains('e') || val.contains('E') {
            return Ok((
                ffi::new_float64(val.parse::<f64>().unwrap_or(0.0))?,
                "float64".to_string(),
            ));
        }
        return Ok(match builtin::try_parse_number(val)? {
            Some((w, lt)) => (w, lt.to_string()),
            None => (ffi::new_int64(0)?, "int64".to_string()),
        });
    }
    Ok((kvkind::new_rwir(0, 0, val)?, kvkind::KIND_RWIR.to_string()))
}

fn is_literal(s: &str) -> bool {
    if s.is_empty() {
        return false;
    }
    let b = s.as_bytes();
    b[0] == b'"'
        || b[0] == b'/'
        || s == "true"
        || s == "false"
        || b[0].is_ascii_digit()
        || (b[0] == b'-' && s.len() > 1)
}

// ── printstack：/vthread 活动栈渲染（帧链 + 每帧实参 + 顶帧当前指令）──────────
//
// 实测帧布局（2026-09）：
//   /vthread/<vid>/‥pc / ‥status / ‥msg             vthread 头（仅根）
//   /vthread/<vid>/[k]/‥lib / ‥callpc / ‥returnpc    第 k 帧（帧目录，k 为帧号）
//   /vthread/<vid>/[k]/[0,±j]                        该帧绑定的实参 / 写参
//   /vthread/<vid>/[k]/[s0,s1]                       帧内指令槽；PC 指向当前那一条
// 只读：不碰 ‥pc/‥status，不写任何槽——暂停/恢复是调用方（harness）的事。

/// 帧目录按帧号升序：`/vthread/<vid>/[k]/`（k 为整数）。
fn frame_dirs(kv: &mut Kv, root: &str) -> Vec<String> {
    let mut v: Vec<(i64, String)> = kv
        .list(&format!("{root}/"), false, false)
        .into_iter()
        .filter_map(|n| {
            let t = n.trim_end_matches('/');
            if !(t.starts_with('[') && t.ends_with(']')) {
                return None;
            }
            t[1..t.len() - 1]
                .parse::<i64>()
                .ok()
                .map(|k| (k, format!("{root}/{t}")))
        })
        .collect();
    v.sort();
    v.into_iter().map(|(_, p)| p).collect()
}

/// 值截断到 200 字符（整份文件内容躺在槽里时不该刷屏）。
fn clip(s: String) -> String {
    let n = s.chars().count();
    if n <= 200 {
        return s;
    }
    let mut out: String = s.chars().take(200).collect();
    out.push('…');
    out
}

/// 读 `‥pc`（`[2]int32`，body 8 字节 = `[stackid:i32 LE][s0:i32 LE]`）。
///
/// **缺失或 body 不是 8 字节一律报错，不返回 (0,0)**：`(0,0)` 是合法位置
/// （第 0 帧第 0 条指令），把「取不到」折成它，就等于让「没有 pc」和「pc 在原点」
/// 说同一句话——printstack 会安静地渲染出一个不存在的位置。
fn pc_of(kv: &mut Kv, key: &str) -> Result<(i32, i32), String> {
    let d = kv.get_one(key);
    if d.is_empty() {
        return Err(format!("{key}: pc 缺失"));
    }
    let body = crate::ffi::xh_body_slice(&d);
    if body.len() != 8 {
        return Err(format!(
            "{key}: pc body 应为 8 字节（[2]int32），实际 {}",
            body.len()
        ));
    }
    let stackid = i32::from_le_bytes([body[0], body[1], body[2], body[3]]);
    let s0 = i32::from_le_bytes([body[4], body[5], body[6], body[7]]);
    Ok((stackid, s0))
}

/// 字符串键的纯值（char 类给内容，其余给 kind:value / 空串）。
fn plain_of(kv: &mut Kv, key: &str) -> String {
    let d = kv.get_one(key);
    if d.is_empty() {
        return String::new();
    }
    let lt = kv.langtype_of(key);
    let k = kvkind::kind(&d, &lt);
    if lt.is_empty() || kvkind::is_char_kind(&k) {
        kvkind::value_string(&d)
    } else {
        let s = kvkind::display(&d, &lt);
        if s == "None" {
            String::new()
        } else {
            s
        }
    }
}

/// printstack：把 /vthread/<vid> 的活动栈渲染成可读文本；vid 必填
/// （「当前 vthread」由调用方从自己的 PC 前缀取，见 runtime 侧 rwir）。
pub fn printstack(kv: &mut Kv, vid: &str) -> String {
    if vid.is_empty() {
        return "error: printstack requires vid".to_string();
    }
    let root = format!("/vthread/{vid}");
    // 头部字段都是字符串值：给纯值（不带 kind 前缀）；槽位值才用 kind:value（类型有意义）。
    let status = plain_of(kv, &format!("{root}/‥status"));
    /* pc 是**单个 key** `/vthread/{vid}/‥pc`，langtype `[2]int32`，
     * body 8 字节 = [stackid:i32 LE][s0:i32 LE]。不是两个成员 key。 */
    let pc = pc_of(kv, &format!("{root}/‥pc"));
    let emsg = plain_of(kv, &format!("{root}/‥msg"));
    let frames = frame_dirs(kv, &root);
    let mut out = String::new();
    out.push_str(&format!(
        "vthread {vid} status={status} frames={}\n",
        frames.len()
    ));
    if !emsg.is_empty() {
        out.push_str(&format!("error: {emsg}\n"));
    }
    match &pc {
        Ok((stackid, s0)) => out.push_str(&format!("pc: stackid={stackid} s0={s0}\n")),
        Err(message) => out.push_str(&format!("pc: error: {message}\n")),
    }
    // 顶帧 = `‥pc[0]`（stackid）指向的那一帧。**不再用「pc 是路径前缀」判定**：
    // pc 是 `[2]int32` 后不再是可 `contains` 的路径，前缀匹配会静默找不到帧
    // （不报错，只是 cur 为空、当前指令不打印）。帧目录形如
    // `/vthread/{vid}/[{stackid}]`，按 `[stackid]` 精确匹配。
    let cur = match &pc {
        Ok((stackid, _)) => {
            let want = format!("[{stackid}]");
            frames
                .iter()
                .rev()
                .find(|f| f.rsplit('/').next() == Some(want.as_str()))
                .cloned()
        }
        Err(_) => None,
    };
    for (i, f) in frames.iter().enumerate() {
        let lib = plain_of(kv, &format!("{f}/‥lib"));
        out.push_str(&format!("frame[{i}] {lib}\n"));
        /*
         * `‥callpc` / `‥returnpc` 与 `‥pc` **同编码**：langtype `[2]int32`，body 8 字节
         * = [stackid:i32 LE][s0:i32 LE]（spec 04-layout语义/05-控制流）。
         *
         * 不能走 `plain_of`：它会按 int32 只取**前 4 字节**，把 stackid 显示成
         * 唯一一个数字，s0 静默消失——**不报错**，看起来像「返回点丢了一位」。
         * 三处共用同一个 `pc_of`，不写第二套解析。
         */
        for key in ["‥callpc", "‥returnpc"] {
            let full = format!("{f}/{key}");
            if kv.get_one(&full).is_empty() {
                continue; // 键不存在是常态（非调用帧没有这两个字段）
            }
            match pc_of(kv, &full) {
                Ok((stackid, s0)) => {
                    out.push_str(&format!("  {key} = stackid={stackid} s0={s0}\n"))
                }
                /* 存在但解不出是**错误**，要看得见——静默跳过等于把它变回「少一位」。 */
                Err(message) => out.push_str(&format!("  {key} = error: {message}\n")),
            }
        }
        // 帧成员 = 实参槽 `[0,±j]` + **命名局部**（变量名即成员名，如 text/total/sz）
        // + `‥*`。命名局部要用 expand_ext 才列得出来（kvspace list 默认不展开）。
        let mut names: Vec<String> = kv
            .list(&format!("{f}/"), true, true)
            .into_iter()
            .map(|n| n.trim_end_matches('/').to_string())
            .collect();
        names.sort();
        for n in names {
            if n.starts_with('‥') {
                continue;
            }
            if n == "[0,0]" {
                continue; // 帧的签名锚点，不是值槽
            }
            let is_arg = n.starts_with("[0,");
            let is_local = !is_arg && !n.starts_with('[');
            if !(is_arg || is_local) {
                continue;
            }
            let mut cur_key = format!("{f}/{n}");
            let mut v = kv.get_one(&cur_key);
            if is_arg {
                for _ in 0..2 {
                    if !kvkind::is_ptr(&v) {
                        break;
                    }
                    cur_key = kvkind::ptr_target(&v);
                    v = kv.get_one(&cur_key);
                }
            }
            let v = kvkind::display(&v, &kv.langtype_of(&cur_key));
            out.push_str(&format!("  {n} = {}\n", clip(v)));
        }
        if let (true, Ok((_, pc_s0))) = (cur.as_deref() == Some(f.as_str()), &pc) {
            /* 当前指令 = **函数指令树**里的 `[s0,0]`，不是帧目录下的：
             * 帧只存实参/写参，指令经 `‥lib` 软链到 `/lib/<fn>/`。s0 是
             * layout 的 irseq，直接就是指令树的坐标。 */
            let lib = plain_of(kv, &format!("{f}/‥lib"));
            let pc_key = format!("{}/[{pc_s0},0]", lib.trim_end_matches('/'));
            let opcode = kv.get_one(&pc_key);
            let lt = kv.langtype_of(&pc_key);
            let kind = kvkind::kind(&opcode, &lt);
            let v = if kind == "rwir" || kind == "rwfunc" {
                format!("{kind}:{}", kvkind::rwir_sig(&opcode))
            } else {
                kvkind::display(&opcode, &lt)
            };
            out.push_str(&format!("  cur = {}\n", clip(v)));
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn roundtrip(src: &str) {
        let once = format(src).unwrap_or_else(|e| panic!("format: {e}\nsrc:\n{src}"));
        assert!(vet(&once).is_ok(), "formatted must vet:\n{once}");
        assert_eq!(format(&once).unwrap(), once, "idempotent fail:\n{once}");
    }

    #[test]
    fn format_preserves_lib() {
        roundtrip("lib http {\n\trwfunc get(*[]char/utf32 $url) -> (*[]char/utf32 $resp) {\n\t\thttp·call(\"GET\", \"\", *url, \"\") -> *resp\n\t}\n}\n");
        roundtrip("lib byteseek {\nlib session {\nlib s1 {\nrwfunc main() -> () {\n3 + 4 -> s\nprintln(s)\n}\n}\n}\n}\nmain()\n");
    }

    #[test]
    fn null_literal_rejected() {
        // 空值只有 None；书写 null 是错误，layout 必须显式拒绝。
        assert!(vet("lib t {\nrwfunc main() -> () {\nnull -> x\n}\n}\n").is_err());
        assert!(vet("lib t {\nrwfunc main() -> () {\nprintln(null)\n}\n}\n").is_err());
        // None、字符串 "null"、含 null 的路径不受影响。
        assert!(vet("lib t {\nrwfunc main() -> () {\nNone -> x\n}\n}\n").is_ok());
        assert!(vet("lib t {\nrwfunc main() -> () {\n\"null\" -> x\n}\n}\n").is_ok());
    }

    #[test]
    fn nested_lib_layout_and_merge() {
        let dsn_nested = format!("fs:///tmp/kvlanglayout_nested_{}", std::process::id());
        // 测试自己建的 store 自己删：残留会累积到 /tmp 撑爆 pod。
        struct Guard_nested(String);
        impl Drop for Guard_nested {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard_nested = Guard_nested(dsn_nested.clone());
        let _ = std::fs::remove_dir_all(dsn_nested.trim_start_matches("fs://"));
        let mut kv = Kv::conn(&dsn_nested);
        init_dirs(&mut kv).unwrap();

        compile(
            &mut kv,
            "lib a {\nlib b {\nrwfunc f() -> (int64 $r) {\n1 -> r\n}\n}\n}\n",
        )
        .unwrap();
        let dir = kv.get_one("/lib/a/b·f/");
        assert_eq!(kvkind::kind(&dir, &kv.langtype_of("/lib/a/b·f/")), "rwfunc");
        assert_eq!(kvkind::head(&dir).storetype, 0);
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/a/b·f/[0,0]"), &kv.langtype_of("/lib/a/b·f/[0,0]")),
            "rwfunc"
        );
        assert_eq!(
            kvkind::head(&kv.get_one("/lib/a/b·f/[0,0]")).storetype,
            1
        );

        // 同 lib a 下再 layout 另一嵌套 lib c，验证 b·f 未被整库删除（增量合并）
        compile(
            &mut kv,
            "lib a {\nlib c {\nrwfunc g() -> (int64 $g) {\n2 -> r\n}\n}\n}\n",
        )
        .unwrap();
        assert_eq!(
            kvkind::kind(
                &kv.get_one("/lib/a/b·f/[0,0]"),
                &kv.langtype_of("/lib/a/b·f/[0,0]")
            ),
            "rwfunc",
            "b·f 应保留"
        );
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/a/c·g/[0,0]"), &kv.langtype_of("/lib/a/c·g/[0,0]")),
            "rwfunc"
        );
    }

    #[test]
    fn sub_arity_contract() {
        assert!(check_sub_arity("sub", 2, 1).is_ok());
        assert!(check_sub_arity("sub", 1, 1).is_err());
        assert!(check_sub_arity("sub", 2, 0).is_err());
        assert!(check_sub_arity("-", 1, 1).is_ok());
    }

    #[test]
    fn call_slots_use_target_type() {
        let dsn_call_kind = format!("fs:///tmp/kvlanglayout_call_kind_{}", std::process::id());
        // 测试自己建的 store 自己删：残留会累积到 /tmp 撑爆 pod。
        struct Guard_call_kind(String);
        impl Drop for Guard_call_kind {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard_call_kind = Guard_call_kind(dsn_call_kind.clone());
        let _ = std::fs::remove_dir_all(dsn_call_kind.trim_start_matches("fs://"));
        let mut kv = Kv::conn(&dsn_call_kind);
        init_dirs(&mut kv).unwrap();
        // `println` 是运行时提供的内建，由它 def rwir 声明（布局期必须有声明来源）。
        kv.set(&[(
            "/lib/println".to_string(),
            kvkind::KIND_DEF_RWIR.to_string(),
            kvkind::new_defrwir(1, 0, false).unwrap(),
        )])
        .unwrap();
        compile(&mut kv, "rwfunc caller() -> () {\ncallee(1) -> y\nprintln(y)\n}\nrwfunc callee(int64 $x) -> (int64 $r) {\nx -> r\n}\n").unwrap();
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/caller/[1,0]"), &kv.langtype_of("/lib/caller/[1,0]")),
            "rwfunc"
        );
        assert_eq!(
            kvkind::counts(&kv.get_one("/lib/caller/[1,0]")),
            (1, 1, false)
        );
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/caller/[2,0]"), &kv.langtype_of("/lib/caller/[2,0]")),
            "rwir"
        );
        assert_eq!(
            kvkind::counts(&kv.get_one("/lib/caller/[2,0]")),
            (1, 0, false)
        );
        compile(&mut kv, "rwfunc later() -> () {\ncallee(1) -> y\n}\n").unwrap();
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/later/[1,0]"), &kv.langtype_of("/lib/later/[1,0]")),
            "rwfunc"
        );
        compile(
            &mut kv,
            "rwfunc absolute() -> () {\n/lib/callee(1) -> y\n}\n",
        )
        .unwrap();
        assert_eq!(
            kvkind::kind(
                &kv.get_one("/lib/absolute/[1,0]"),
                &kv.langtype_of("/lib/absolute/[1,0]")
            ),
            "rwfunc"
        );
        compile(
            &mut kv,
            "lib p {\nrwfunc caller() -> () {\np·target()\n}\nrwfunc target() -> () {}\n}\n",
        )
        .unwrap();
        assert_eq!(
            kvkind::kind(
                &kv.get_one("/lib/p·caller/[1,0]"),
                &kv.langtype_of("/lib/p·caller/[1,0]")
            ),
            "rwfunc"
        );
    }

    #[test]
    fn struct_definition_uses_child_keys() {
        let dsn_struct = format!("fs:///tmp/kvlanglayout_struct_{}", std::process::id());
        // 测试自己建的 store 自己删：残留会累积到 /tmp 撑爆 pod。
        struct Guard_struct(String);
        impl Drop for Guard_struct {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard_struct = Guard_struct(dsn_struct.clone());
        let _ = std::fs::remove_dir_all(dsn_struct.trim_start_matches("fs://"));
        let mut kv = Kv::conn(&dsn_struct);
        init_dirs(&mut kv).unwrap();
        compile(
            &mut kv,
            "struct Point {\n\tx:int64=3\n\tnext:*Point=None\n}\n",
        )
        .unwrap();
        let proto = kv.get_one("/lib/Point");
        assert_eq!(kvkind::kind(&proto, &kv.langtype_of("/lib/Point")), "def struct");
        assert!(kvkind::body(&proto, &kvkind::head(&proto)).is_empty());
        assert_eq!(kvkind::value_string(&kv.get_one("/lib/Point/x")), "int64");
        assert_eq!(
            kvkind::value_string(&kv.get_one("/lib/Point/next")),
            "*/lib/Point"
        );
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/Point·x"), &kv.langtype_of("/lib/Point·x")),
            "int64"
        );
        assert!(kv.get_one("/lib/Point·next").is_empty());
    }

    #[test]
    fn dot_is_ordinary_char_contiguous_coords() {
        // '.' 是普通字符（小数点/后缀等），foo.bar 视作单一 opcode，不再被切成 foo bar 两 token
        // 而摊出畸形坐标（#106）。断言：opcode 原样保留，且指令坐标连续、无空缺行。
        // #116 flat 模型：终结符补全后 [3,0] 为 terminate 追加的 return，[4,0] 才是帧末尾。
        let dsn_dot = format!("fs:///tmp/kvlanglayout_dot_{}", std::process::id());
        // 测试自己建的 store 自己删：残留会累积到 /tmp 撑爆 pod。
        struct Guard_dot(String);
        impl Drop for Guard_dot {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard_dot = Guard_dot(dsn_dot.clone());
        let _ = std::fs::remove_dir_all(dsn_dot.trim_start_matches("fs://"));
        let mut kv = Kv::conn(&dsn_dot);
        init_dirs(&mut kv).unwrap();
        // `baz` 是外部内建：裸名必须有声明来源（def rwir），否则布局期报错。
        kv.set(&[(
            "/lib/baz".to_string(),
            kvkind::KIND_DEF_RWIR.to_string(),
            kvkind::new_defrwir(1, 1, false).unwrap(),
        )])
        .unwrap();
        compile(
            &mut kv,
            "lib t {\nrwfunc main() -> () {\nfoo.bar(\"x\") -> y\nbaz(\"z\") -> w\n}\n}\n",
        )
        .unwrap();
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/t·main/[0,0]"), &kv.langtype_of("/lib/t·main/[0,0]")),
            "rwfunc"
        );
        assert!(
            kvkind::value_string(&kv.get_one("/lib/t·main/[1,0]")).contains("foo.bar"),
            "'.' 应作普通字符保留在 opcode 内（单一 token foo.bar）"
        );
        assert!(
            !kv.get_one("/lib/t·main/[2,0]").is_empty(),
            "[2,0] 应有指令：坐标不得空缺"
        );
        assert!(
            kvkind::value_string(&kv.get_one("/lib/t·main/[3,0]")).contains("return"),
            "[3,0] 应为 terminate 追加的 return（连续无空缺）"
        );
        assert!(
            kv.get_one("/lib/t·main/[4,0]").is_empty(),
            "[4,0] 应为帧末尾（连续无空缺）"
        );
    }

    #[test]
    fn fallthrough_into_label_is_error() {
        let mut seq = Vec::new();
        let mut labels = HashMap::new();
        let r = collect_insts(
            &[
                Stmt::Instruction(Instruction {
                    expr: Some(crate::ast::leaf("1")),
                    writes: vec!["x".into()],
                    ..Default::default()
                }),
                Stmt::Scope(crate::ast::ScopeStmt {
                    comments: Vec::new(),
                    label: "_open".into(),
                    synthetic: false,
                    body: vec![Stmt::Instruction(Instruction {
                        expr: Some(crate::ast::leaf("return")),
                        ..Default::default()
                    })],
                }),
            ],
            &mut seq,
            &mut labels,
        );
        assert!(r.unwrap_err().contains("falls through into block label"));
    }

    #[test]
    fn named_block_falls_through_without_return() {
        /* 用户具名块之后还有语句时，块体末尾**不得**补 `return`：线性展平后
         * 下一条语句就是它的续接。旧实现递归 terminate 每个 Scope，把 return
         * 插在序列中间，后面所有语句不可达（`local/shadowing.kv` 实测）。
         *
         * 正向判据只数 `return` 的个数：旧实现 2 条，正确实现 1 条且在最末。 */
        let dsn = format!("fs:///tmp/kvlanglayout_named_block_{}", std::process::id());
        struct Guard(String);
        impl Drop for Guard {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard = Guard(dsn.clone());
        let _ = std::fs::remove_dir_all(dsn.trim_start_matches("fs://"));
        let mut kv = Kv::conn(&dsn);
        init_dirs(&mut kv).unwrap();
        compile(
            &mut kv,
            "lib t {\nrwfunc main() -> () {\n1 -> x\ninner: {\n2 -> x\n}\n3 -> x\n}\n}\n",
        )
        .unwrap();
        let mut returns = Vec::new();
        let mut last_opcode = String::new();
        let mut ordinal = 1;
        loop {
            let key = format!("/lib/t·main/[{ordinal},0]");
            let value = kv.get_one(&key);
            if value.is_empty() {
                break;
            }
            let opcode = kvkind::value_string(&value);
            if opcode.contains("return") {
                returns.push(ordinal);
            }
            last_opcode = opcode;
            ordinal += 1;
        }
        assert!(last_opcode.contains("return"), "函数级隐式 return 必须保留");
        assert_eq!(
            returns.len(),
            1,
            "具名块不应额外产生 return；实际 return 出现在 irseq {returns:?}"
        );
    }

    #[test]
    fn unterminated_block_is_error() {
        let mut seq = Vec::new();
        let mut labels = HashMap::new();
        let r = collect_insts(
            &[Stmt::Scope(crate::ast::ScopeStmt {
                comments: Vec::new(),
                label: "_open".into(),
                synthetic: true,
                body: vec![Stmt::Instruction(Instruction {
                    expr: Some(crate::ast::leaf("1")),
                    writes: vec!["x".into()],
                    ..Default::default()
                })],
            })],
            &mut seq,
            &mut labels,
        );
        assert!(r.unwrap_err().contains("does not end with return/goto/br"));
    }
}
