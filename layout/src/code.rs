//! layoutcode（对齐 layout/layout.go）：检查 AST 并把结果布局写到 /lib/ 下的结构化 KV。
//!
//! 存储约定：
//!   /lib/<pkg>·<name>/[0,0]         布局后签名锚点（kind=rwfunc，body=计数头 [nr,nw,dyn]）
//!   /lib/<pkg>·<name>.[0,±k]        参数定义键（langtype=def langtype, body=名字\x00类型）
//!   /lib/<pkg>·<name>/[i,j]         编译后指令（kind=rwir），i 从 1 开始
//!   /lib/<pkg>·<name>/‥labels/<l>   label → irseq
//!   /lib/<pkg>·<name>.src           源码副本（仅 write_func 保留写入，dump 不再依赖）
//!
//! WriteBody: DFS-number insts (incl. ScopeStmt), emit [i,j], rewrite goto/br labels to irseq.
//! dump: 反向——严格读 /lib/<pkg> 子树重建 AST（签名读参数定义键 .[0,±k]、体读线性槽+‥labels），
//!       不读 .src、不依赖签名行 [0,x] 静态槽。

use std::collections::HashMap;

use super::ast::{
    self, Expr, Func, FuncSig, Instruction, Param, RwirDecl, ScopeStmt, Stmt, StructDecl,
};
use super::ffi::Kv;
use super::{builtin, ffi, keytree, kvkind, langtype, lower, parser};

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
    let (file, mut diags) = parser::parse_code(src)?;
    for f in subscript_check_funcs(&file) {
        diags.extend(lower::check_container_subscript(&f));
        diags.extend(lower::check_map_defined(&f));
        diags.extend(lower::check_container_typed(&f));
    }
    for d in &diags {
        eprintln!("{}", d.string());
    }
    if parser::has_errors(&diags) {
        return Err("parse: error-level diagnostics — refusing to load".to_string());
    }

    let mut any_code = false;
    let mut inits: Vec<String> = Vec::new();
    for decl in &file.structs {
        write_struct_decl(kv, decl);
        any_code = true;
    }
    for func in &file.funcs {
        let pkg = if func.pkg.is_empty() {
            file.package.clone()
        } else {
            func.pkg.clone()
        };
        let mut lowered = lower::lower_func(func);
        write_func(kv, &pkg, &mut lowered);
        any_code = true;
        if func.sig.name == "init" {
            inits.push(init_fn_name(&pkg));
        }
    }
    for decl in &file.rwir_decls {
        write_rwir_decl(kv, decl);
    }

    let mut body = file.init_body.clone();
    for c in &file.top_level_calls {
        body.push(Stmt::Instruction(c.clone()));
    }
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
        let mut lowered = lower::lower_func(&init_fn);
        write_func(kv, "", &mut lowered);
        any_code = true;
        inits.push(init_fn_name(""));
    }

    if !any_code {
        return Err("no executable code found".to_string());
    }
    Ok(inits)
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
pub fn vet(src: &str) -> Result<(), String> {
    let (file, mut diags) = parser::parse_code(src)?;
    for f in subscript_check_funcs(&file) {
        diags.extend(lower::check_container_subscript(&f));
        diags.extend(lower::check_map_defined(&f));
        diags.extend(lower::check_container_typed(&f));
    }
    for d in &diags {
        eprintln!("{}", d.string());
    }
    if parser::has_errors(&diags) {
        return Err("parse: error-level diagnostics — refusing to load".to_string());
    }
    let mut any_code = false;
    for func in &file.funcs {
        let _ = lower::lower_func(func);
        any_code = true;
    }
    if !file.init_body.is_empty() || !file.top_level_calls.is_empty() || !file.structs.is_empty() {
        any_code = true;
    }
    if !any_code {
        return Err("no executable code found".to_string());
    }
    Ok(())
}

/// dump：把 /lib 子树重构为可运行的 kvlang 源码（还原 `lib {}` 与 `rwfunc`），
/// lower 后的原始槽位（`key kind:value`）以 `#` 注释附在各自函数后，供审查。
pub fn dump(kv: &mut Kv, lib: &str) -> String {
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
            let v = kv.get_one(&format!("{prefix}{base}"));
            decls.push(format!("{prefix}{base} {}", sanitize(&kvkind::display(&v))));
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
            out.push(format!("{c} {}", sanitize(&kvkind::display(&v))));
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

// ── dump 重建：严格从 /lib 子树反出可运行 kvlang（不读 .src）─────────────
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
            };
        }
    }
    for k in 1..=nw {
        if let Some((rname, rty)) = kvkind::def_param_parts(&kv.get_one(&format!("{base}.[0,{k}]")))
        {
            returns[(k - 1) as usize] = Param {
                name: rname,
                ty: rty,
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
        .filter_map(|c| {
            let irseq: i32 = kvkind::plain(&kv.get_one(&format!("{ldir}{c}")))
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
            let d = kv.get_one(&format!("{dir}[{n},-{j}]"));
            if d.is_empty() {
                break;
            }
            reads.push(decode_operand(&d));
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
fn decode_operand(data: &[u8]) -> Operand {
    let k = kvkind::kind(data);
    if matches!(k.as_str(), "rwir" | "rwir|rwfunc" | "rwfunc" | "def rwir") {
        Operand::Ref(kvkind::rwir_sig(data))
    } else if kvkind::is_char_kind(&k) {
        Operand::Str(kvkind::plain(data))
    } else {
        Operand::Lit(kvkind::plain(data))
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
fn build_body(insts: &[RawInst], labels: &[(i32, String)]) -> Vec<Stmt> {
    let by_irseq: HashMap<i32, String> = labels.iter().map(|(i, l)| (*i, l.clone())).collect();
    let total = insts.len() as i32;
    let first = labels.first().map(|(i, _)| *i).unwrap_or(total + 1);
    let inst_stmt = |i: i32| Stmt::Instruction(build_inst(&insts[i as usize - 1], &by_irseq));
    let mut body: Vec<Stmt> = (1..first).map(inst_stmt).collect();
    for (bi, (start, label)) in labels.iter().enumerate() {
        let end = labels.get(bi + 1).map(|(i, _)| *i).unwrap_or(total + 1);
        body.push(Stmt::Scope(ScopeStmt {
            comments: Vec::new(),
            label: label.clone(),
            body: (*start..end).map(inst_stmt).collect(),
        }));
    }
    body
}

/// (opcode, reads, writes) → Instruction（箭头规范化为 `->`；复用 Display 还原算子/糖）。
fn build_inst(inst: &RawInst, by_irseq: &HashMap<i32, String>) -> Instruction {
    let expr = match inst.opcode.as_str() {
        "" => None,
        "return" => Some(ast::leaf("return")),
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
    let is_fill = inst.opcode == "array·fill" && !inst.writes.is_empty();
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
    }
}

/// 写函数到 /lib/：签名（rwfunc）、源码、参数 Ptr、指令体。
pub fn write_func(kv: &mut Kv, pkg: &str, fn_: &mut Func) {
    let mut type_map = lower::infer_types(fn_);
    lower::specialize(fn_, &type_map);
    let func_dir = keytree::lib_func(pkg, &fn_.sig.name);

    let mut seq: Vec<Instruction> = Vec::new();
    let mut labels: HashMap<String, i32> = HashMap::new();
    collect_insts(&fn_.body, &mut seq, &mut labels);

    // 形参引用 → 帧坐标（编译期替换，运行时无命名参数角色）。带 `*` 的形参按**地址传递**（槽存实参地址，体内显式解引用 `*[0,-k]`）；
    // 不带 `*` 的按**值传递**（槽存值本体，体内裸坐标 `[0,-k]` 直读）。见 spec [[函数]]。
    let mut param_coord: HashMap<String, String> = HashMap::new();
    for (i, p) in fn_.sig.params.iter().enumerate() {
        let d = if langtype::is_addr_param(&p.ty) { "*" } else { "" };
        param_coord.insert(p.name.clone(), format!("{d}[0,-{}]", i + 1));
    }
    for (i, r) in fn_.sig.returns.iter().enumerate() {
        let d = if langtype::is_addr_param(&r.ty) { "*" } else { "" };
        param_coord.insert(r.name.clone(), format!("{d}[0,{}]", i + 1));
    }

    // 按函数覆盖（文件夹复制式合并）：只 del_tree 本函数子树，不动 /lib 下其它函数。
    // 禁止整库删除——layoutcode 必须可增量：多次 layout 各自覆盖其函数，不误删先前的函数。
    let _ = kv.del_tree(&func_dir);
    let _ = kv.mkindex(&format!("{func_dir}/"));

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

    let mut pairs: Vec<(String, Vec<u8>)> = Vec::new();
    pairs.push((
        format!("{func_dir}/[0,0]"),
        kvkind::new_rwfunc(seq.len() as i32, nr, nw, fn_.sig.dynamic()),
    ));
    pairs.push((
        keytree::lib_src(pkg, &fn_.sig.name),
        ffi::new_char_byte(fn_.full_text().as_bytes()),
    ));
    // 参数定义键 funcDir.[0,±k]（点后缀，与坐标斜杠键 /[0,±k] 区分）：langtype=def langtype，
    // body=名字\x00类型串。坐标斜杠键 [0,±k] 留给 runtime call 期写实参地址 Ptr；点后缀键
    // 不同名、不触发 extindex 写保护。函数体形参引用已替换为 *[0,±k] 显式解引用。
    for (i, p) in fn_.sig.params.iter().enumerate() {
        pairs.push((
            format!("{func_dir}.[0,-{}]", i + 1),
            kvkind::new_def_param(&p.name, &param_types[i]),
        ));
    }
    for (i, r) in fn_.sig.returns.iter().enumerate() {
        pairs.push((
            format!("{func_dir}.[0,{}]", i + 1),
            kvkind::new_def_param(&r.name, &param_types[nr as usize + i]),
        ));
    }
    let _ = kv.set(&pairs);

    for (i, inst) in seq.iter().enumerate() {
        write_linear_inst(
            kv,
            &func_dir,
            (i as i32) + 1,
            inst,
            &labels,
            &mut type_map,
            &param_coord,
            &param_langtype,
        );
    }
    if !labels.is_empty() {
        let _ = kv.mkindex(&keytree::lib_labels_dir(pkg, &fn_.sig.name));
        let lpairs: Vec<(String, Vec<u8>)> = labels
            .iter()
            .map(|(label, irseq)| {
                (
                    keytree::lib_label(pkg, &fn_.sig.name, label),
                    ffi::new_int64(*irseq as i64),
                )
            })
            .collect();
        let _ = kv.set(&lpairs);
    }
}

/// 写 struct 原型到 /lib/<Name>：基节点(kind=struct) + memindex(/lib/Name·) + 各字段默认值。
/// 运行时 struct·new 以此为原型 cplist 克隆，故字段默认值即实例初值。
pub fn write_struct_decl(kv: &mut Kv, decl: &StructDecl) {
    let mut name = decl.name.clone();
    if !decl.pkg.is_empty() {
        name = format!("{}{}{name}", decl.pkg, keytree::MEMBER_SEP);
    }
    let base = keytree::rwir(&name);
    let _ = kv.del_tree(&base);
    let fnames: Vec<String> = decl.fields.iter().map(|f| f.name.clone()).collect();
    let ftypes: Vec<(String, String)> = decl
        .fields
        .iter()
        .map(|f| (f.name.clone(), f.ty.clone()))
        .collect();
    let mut pairs: Vec<(String, Vec<u8>)> = Vec::new();
    pairs.push((base.clone(), kvkind::new_struct(&ftypes)));
    pairs.push((keytree::member(&base, ""), kvkind::new_memindex(&fnames)));
    for fld in &decl.fields {
        // *T 字段的默认值是空指针 None——不落键。None 在 kvspace 即"无值"（键不存在），
        // 故原型不带该字段、实例读回 None（见 [[ptr]]）；不造"空 Ptr"这第二种空值表示。
        if fld.ty.starts_with('*') {
            continue;
        }
        pairs.push((
            keytree::member(&base, &fld.name),
            field_default(&fld.ty, fld.default.as_ref()),
        ));
    }
    let _ = kv.set(&pairs);
}

/// 字段默认值 XValue：head kind = 字段类型，body = 默认字面量（未给则零值）。
/// 标量+char 直接编码；带 dims / structref 仅记录类型（空 body），嵌套 struct 待定。
fn field_default(ty: &str, default: Option<&Expr>) -> Vec<u8> {
    // *T 指针字段不在此列——write_struct_decl 已按"空指针 = None = 不落键"跳过。
    let (dims, base) = kvkind::parse_langtype(ty);
    let s = default.map(|e| e.val.clone()).unwrap_or_default();
    if base.starts_with("char/") {
        return ffi::new_char(&base, &s);
    }
    if !dims.is_empty() {
        return ffi::tlv_encode(&base, &[], dims.iter().product());
    }
    let i = || s.parse::<i64>().unwrap_or(0);
    let u = || s.parse::<u64>().unwrap_or(0);
    let f = || s.parse::<f64>().unwrap_or(0.0);
    match base.as_str() {
        "bool" => ffi::new_bool(s == "true"),
        "int8" => ffi::tlv_encode("int8", &(i() as i8).to_le_bytes(), 1),
        "int16" => ffi::tlv_encode("int16", &(i() as i16).to_le_bytes(), 1),
        "int32" => ffi::tlv_encode("int32", &(i() as i32).to_le_bytes(), 1),
        "int64" => ffi::new_int64(i()),
        "uint8" => ffi::tlv_encode("uint8", &[u() as u8], 1),
        "uint16" => ffi::tlv_encode("uint16", &(u() as u16).to_le_bytes(), 1),
        "uint32" => ffi::tlv_encode("uint32", &(u() as u32).to_le_bytes(), 1),
        "uint64" => ffi::tlv_encode("uint64", &u().to_le_bytes(), 1),
        "float32" => ffi::tlv_encode("float32", &(f() as f32).to_le_bytes(), 1),
        "float64" => ffi::new_float64(f()),
        _ => ffi::tlv_encode(&base, &[], 1),
    }
}

/// 写用户声明的 rwir（无体）到 /lib/<opcode>。
pub fn write_rwir_decl(kv: &mut Kv, decl: &RwirDecl) {
    let mut opcode = decl.sig.name.clone();
    if !decl.pkg.is_empty() {
        opcode = format!("{}{}{opcode}", decl.pkg, keytree::MEMBER_SEP);
    }
    let nr = decl.sig.num_reads();
    let nw = decl.sig.num_writes();
    let param_types = decl.sig.langtype_list();
    let base = keytree::rwir(&opcode);
    // 路由头：仅计数头（无参数载荷）；各参数类型落 [0,x] 签名行槽（def langtype）。
    let mut pairs: Vec<(String, Vec<u8>)> = vec![(
        base.clone(),
        kvkind::new_defrwir(nr, nw, decl.sig.dynamic()),
    )];
    for i in 0..nr as usize {
        pairs.push((
            format!("{base}/[0,-{}]", i + 1),
            kvkind::new_def_langtype(&param_types[i]),
        ));
    }
    for i in 0..nw as usize {
        pairs.push((
            format!("{base}/[0,{}]", i + 1),
            kvkind::new_def_langtype(&param_types[nr as usize + i]),
        ));
    }
    let _ = kv.set(&pairs);
}

/// Flatten body into seq; ScopeStmt records label → irseq (1-based; [0,0] is the signature).
fn collect_insts(body: &[Stmt], seq: &mut Vec<Instruction>, labels: &mut HashMap<String, i32>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => seq.push(s.clone()),
            Stmt::Scope(s) => {
                if s.label.is_empty() {
                    panic!("WriteBody: ScopeStmt with empty label");
                }
                if labels.contains_key(&s.label) {
                    panic!("WriteBody: duplicate label {}", s.label);
                }
                let start = seq.len() as i32 + 1;
                if start > 1 && !inst_is_terminator(seq.last().unwrap()) {
                    panic!("WriteBody: fall through into label {}", s.label);
                }
                labels.insert(s.label.clone(), start);
                collect_insts(&s.body, seq, labels);
                if (seq.len() as i32) < start || !inst_is_terminator(seq.last().unwrap()) {
                    panic!("WriteBody: unterminated block {}", s.label);
                }
            }
            _ => panic!("WriteBody: unexpected stmt {} after lower", st.first_line()),
        }
    }
}

fn write_linear_inst(
    kv: &mut Kv,
    prefix: &str,
    n: i32,
    s: &Instruction,
    labels: &HashMap<String, i32>,
    type_map: &mut HashMap<String, String>,
    params: &HashMap<String, String>,
    param_langtype: &HashMap<String, String>,
) {
    for (j, w) in s.writes.iter().enumerate() {
        if j < s.write_types.len() && !s.write_types[j].is_empty() {
            type_map.insert(w.clone(), s.write_types[j].clone());
        }
    }
    let (opcode, mut reads) = s.flat();
    match opcode.as_str() {
        "goto" => {
            if reads.len() != 1 {
                panic!("WriteBody: goto expects 1 read, got {}", reads.len());
            }
            reads[0] = resolve_label(labels, &reads[0], "goto").to_string();
        }
        "br" => {
            if reads.len() != 3 {
                panic!("WriteBody: br expects 3 reads, got {}", reads.len());
            }
            reads[1] = resolve_label(labels, &reads[1], "br").to_string();
            reads[2] = resolve_label(labels, &reads[2], "br").to_string();
        }
        _ => {}
    }
    let target_char = if s.writes.len() == 1
        && !s.write_types.is_empty()
        && kvkind::is_char_kind(&s.write_types[0])
    {
        s.write_types[0].as_str()
    } else {
        ""
    };

    let mut pairs: Vec<(String, Vec<u8>)> = Vec::with_capacity(1 + reads.len() + s.writes.len());
    if !opcode.is_empty() {
        pairs.push((format!("{prefix}/[{n},0]"), opcode_value(&opcode)));
    }
    for (j, r) in reads.iter().enumerate() {
        let rv = params
            .get(r.as_str())
            .map(String::as_str)
            .unwrap_or(r.as_str());
        pairs.push((
            format!("{prefix}/[{n},-{}]", j + 1),
            slot_value(rv, target_char),
        ));
    }
    for (j, w) in s.writes.iter().enumerate() {
        let orig = w.as_str();
        let wv = params.get(orig).map(String::as_str).unwrap_or(orig);
        let ty = s
            .write_types
            .get(j)
            .map(String::as_str)
            .filter(|t| !t.is_empty())
            .or_else(|| param_langtype.get(orig).map(String::as_str))
            .unwrap_or("");
        pairs.push((
            format!("{prefix}/[{n},{}]", j + 1),
            write_slot_value(wv, ty),
        ));
    }
    if !pairs.is_empty() {
        let _ = kv.set(&pairs);
    }
}

fn inst_is_terminator(inst: &Instruction) -> bool {
    match &inst.expr {
        Some(e) if e.is_leaf() => e.val == "return",
        Some(e) => matches!(e.op.as_str(), "return" | "goto" | "br"),
        None => false,
    }
}

fn resolve_label(labels: &HashMap<String, i32>, name: &str, opcode: &str) -> i32 {
    match labels.get(name) {
        Some(&irseq) => irseq,
        None => panic!("WriteBody: {opcode} unknown label {name:?}"),
    }
}

/// 调用目标 opcode 槽值：函数调用/运算符 → `rwir|rwfunc` 并列；
/// 控制/拷贝 opcode（return/goto/br/call/=）原样 `rwir`（非调用目标）。
fn opcode_value(opcode: &str) -> Vec<u8> {
    if matches!(opcode, "return" | "goto" | "br" | "call" | "=") {
        kvkind::new_rwir(0, 0, opcode)
    } else {
        kvkind::new_rwir_union(opcode)
    }
}

/// 写槽值：写目标带 **map langtype** 标注时（`x:{keylt}·{valt} = {}`），槽的 langtype 即该 map
/// langtype、body 即变量名——runtime 据此把容器值按 [[map容器]] 落成「langtype=map langtype、
/// storetype=index、body 空」，成员索引落兄弟槽 `x·`。其余写槽仍是 rwir 引用（body 带计数头）。
/// 参数替换过的写槽（rwfunc 写参轴）不承载类型：声明类型已落 `/lib/<fn>.[0,k]` 参数定义键。
fn write_slot_value(name: &str, ty: &str) -> Vec<u8> {
    if ty.contains(keytree::MEMBER_SEP) {
        return ffi::tlv_encode(ty, name.as_bytes(), 1);
    }
    slot_value(name, "")
}

/// 将字面量/引用字符串编码为 XValue TLV（rwir 槽值）。
fn slot_value(val: &str, target_char: &str) -> Vec<u8> {
    if !is_literal(val) {
        return kvkind::new_rwir(0, 0, val);
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
        return ffi::new_char(k, s);
    }
    if val == "true" || val == "false" {
        return ffi::new_bool(val == "true");
    }
    if b[0].is_ascii_digit() || (b[0] == b'-' && val.len() > 1) {
        if val.contains('.') || val.contains('e') || val.contains('E') {
            return ffi::new_float64(val.parse::<f64>().unwrap_or(0.0));
        }
        return builtin::try_parse_number(val).unwrap_or_else(|| ffi::new_int64(0));
    }
    kvkind::new_rwir(0, 0, val)
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
        roundtrip("lib http {\n\trwfunc get(url:[]char/utf32) -> (resp:[]char/utf32) {\n\t\thttp·call(\"GET\", \"\", url, \"\") -> resp\n\t}\n}\n");
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
        let mut kv = Kv::conn(&format!(
            "fs:///tmp/kvlanglayout_nested_{}",
            std::process::id()
        ));
        init_dirs(&mut kv).unwrap();

        compile(
            &mut kv,
            "lib a {\nlib b {\nrwfunc f() -> (r:int64) {\n1 -> r\n}\n}\n}\n",
        )
        .unwrap();
        assert_eq!(kvkind::kind(&kv.get_one("/lib/a/b·f/[0,0]")), "rwfunc");

        // 同 lib a 下再 layout 另一嵌套 lib c，验证 b·f 未被整库删除（增量合并）
        compile(
            &mut kv,
            "lib a {\nlib c {\nrwfunc g() -> (r:int64) {\n2 -> r\n}\n}\n}\n",
        )
        .unwrap();
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/a/b·f/[0,0]")),
            "rwfunc",
            "b·f 应保留"
        );
        assert_eq!(kvkind::kind(&kv.get_one("/lib/a/c·g/[0,0]")), "rwfunc");
    }

    #[test]
    fn dot_is_ordinary_char_contiguous_coords() {
        // '.' 是普通字符（小数点/后缀等），foo.bar 视作单一 opcode，不再被切成 foo bar 两 token
        // 而摊出畸形坐标（#106）。断言：opcode 原样保留，且指令坐标连续、无空缺行。
        // #116 flat 模型：终结符补全后 [3,0] 为 terminate 追加的 return，[4,0] 才是帧末尾。
        let mut kv = Kv::conn(&format!(
            "fs:///tmp/kvlanglayout_dot_{}",
            std::process::id()
        ));
        init_dirs(&mut kv).unwrap();
        compile(
            &mut kv,
            "lib t {\nrwfunc main() -> () {\nfoo.bar(\"x\") -> y\nbaz(\"z\") -> w\n}\n}\n",
        )
        .unwrap();
        assert_eq!(kvkind::kind(&kv.get_one("/lib/t·main/[0,0]")), "rwfunc");
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
    #[should_panic(expected = "fall through into label")]
    fn fallthrough_into_label_panics() {
        let mut seq = Vec::new();
        let mut labels = HashMap::new();
        collect_insts(
            &[
                Stmt::Instruction(Instruction {
                    expr: Some(crate::ast::leaf("1")),
                    writes: vec!["x".into()],
                    ..Default::default()
                }),
                Stmt::Scope(crate::ast::ScopeStmt {
                    comments: Vec::new(),
                    label: "_open".into(),
                    body: vec![Stmt::Instruction(Instruction {
                        expr: Some(crate::ast::leaf("return")),
                        ..Default::default()
                    })],
                }),
            ],
            &mut seq,
            &mut labels,
        );
    }

    #[test]
    #[should_panic(expected = "unterminated block")]
    fn unterminated_block_panics() {
        let mut seq = Vec::new();
        let mut labels = HashMap::new();
        collect_insts(
            &[Stmt::Scope(crate::ast::ScopeStmt {
                comments: Vec::new(),
                label: "_open".into(),
                body: vec![Stmt::Instruction(Instruction {
                    expr: Some(crate::ast::leaf("1")),
                    writes: vec!["x".into()],
                    ..Default::default()
                })],
            })],
            &mut seq,
            &mut labels,
        );
    }
}
