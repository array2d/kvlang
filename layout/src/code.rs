//! layoutcode（对齐 layout/layout.go 的编译期部分）：把 AST 写到 /lib/ 下的结构化 KV。
//!
//! 存储约定：
//!   /lib/<pkg>·<name>/[0,0]         编译后签名（kind=rwfunc）
//!   /lib/<pkg>·<name>/<param>       命名参数→slot 指针（kind=char, isptr=1）
//!   /lib/<pkg>·<name>/[i,j]         编译后指令（kind=rwir），i 从 1 开始
//!   /lib/<pkg>·<name>/‥labels/<l>   label → irseq
//!   /lib/<pkg>·<name>.src           源码副本
//!
//! WriteBody: DFS-number insts (incl. ScopeStmt), emit [i,j], rewrite goto/br labels to irseq.

use std::collections::HashMap;

use super::ast::{Func, Instruction, RwirDecl, Stmt};
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
    let (file, mut diags) = parser::parse_code(src)?;
    for f in subscript_check_funcs(&file) {
        diags.extend(lower::check_container_subscript(&f));
    }
    for d in &diags {
        eprintln!("{}", d.string());
    }
    if parser::has_errors(&diags) {
        return Err("parse: error-level diagnostics — refusing to load".to_string());
    }

    let mut any_code = false;
    let mut inits: Vec<String> = Vec::new();
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
    if !file.init_body.is_empty() || !file.top_level_calls.is_empty() {
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
        let src = kvkind::value_string(&kv.get_one(&format!("{prefix}.src")));
        let mut slots = Vec::new();
        collect_slots(kv, &format!("{prefix}/"), &mut slots);
        funcs.push(DumpFunc {
            pkg: fpkg,
            name,
            src,
            slots,
            dir: format!("{prefix}/"),
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

/// 一个可运行函数：源码（.src）+ 原始槽位注释。
struct DumpFunc {
    pkg: String,
    name: String,
    src: String,
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
            let src = kvkind::value_string(&kv.get_one(&format!("{prefix}{base}.src")));
            let mut slots = Vec::new();
            collect_slots(kv, &dir_sub, &mut slots);
            funcs.push(DumpFunc {
                pkg: fpkg,
                name,
                src,
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

/// 注释不允许换行：defrwfunc 的参数类型以 \n 连接，改 " | " 呈现。
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
    for line in f.src.lines() {
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

/// 写函数到 /lib/：签名（rwfunc）、源码、参数 Ptr、指令体。
pub fn write_func(kv: &mut Kv, pkg: &str, fn_: &mut Func) {
    let mut type_map = lower::infer_types(fn_);
    lower::specialize(fn_, &type_map);
    let func_dir = keytree::lib_func(pkg, &fn_.sig.name);

    // pkg 下同名 func 已存在（含签名槽 [0,0]）→ debug 提示即将覆盖（严格按 lib 声明判定，非路径）。
    // 覆盖是 KVLANG_LIB 引导的常态（stdlib 每次重铺），故降到 debug 级：仅 LOG_LEVEL=debug 显示。
    if !kv.get_one(&format!("{func_dir}/[0,0]")).is_empty()
        && std::env::var("LOG_LEVEL").as_deref() == Ok("debug")
    {
        let qual = if pkg.is_empty() {
            fn_.sig.name.clone()
        } else {
            format!("{pkg}/{}", fn_.sig.name)
        };
        eprintln!("debug: func {qual} already defined — overwriting");
    }

    let mut seq: Vec<Instruction> = Vec::new();
    let mut labels: HashMap<String, i32> = HashMap::new();
    collect_insts(&fn_.body, &mut seq, &mut labels);

    // 按函数覆盖（文件夹复制式合并）：只 del_tree 本函数子树，不动 /lib 下其它函数。
    // 禁止整库删除——layoutcode 必须可增量：多次 layout 各自覆盖其函数，不误删先前的函数。
    let _ = kv.del_tree(&func_dir);
    let _ = kv.mkindex(&format!("{func_dir}/"));

    let nr = fn_.sig.num_reads();
    let nw = fn_.sig.num_writes();
    let param_types: Vec<String> = fn_.sig.kindexp_list();

    let mut pairs: Vec<(String, Vec<u8>)> = Vec::new();
    pairs.push((
        format!("{func_dir}/[0,0]"),
        kvkind::new_rwfunc(seq.len() as i32, nr, nw, &param_types),
    ));
    pairs.push((
        keytree::lib_src(pkg, &fn_.sig.name),
        ffi::new_char_byte(fn_.full_text().as_bytes()),
    ));
    for (i, p) in fn_.sig.params.iter().enumerate() {
        let slot = format!("[0,-{}]", i + 1);
        pairs.push((
            format!("{func_dir}/{}", p.name),
            ffi::new_ptr(kvkind::KIND_CHAR, &slot),
        ));
    }
    for (i, r) in fn_.sig.returns.iter().enumerate() {
        let slot = format!("[0,{}]", i + 1);
        pairs.push((
            format!("{func_dir}/{}", r.name),
            ffi::new_ptr(kvkind::KIND_CHAR, &slot),
        ));
    }
    let _ = kv.set(&pairs);

    for (i, inst) in seq.iter().enumerate() {
        write_linear_inst(kv, &func_dir, (i as i32) + 1, inst, &labels, &mut type_map);
    }
    if !labels.is_empty() {
        let _ = kv.mkindex(&keytree::lib_labels_dir(pkg, &fn_.sig.name));
        let lpairs: Vec<(String, Vec<u8>)> = labels
            .iter()
            .map(|(label, irseq)| (keytree::lib_label(pkg, &fn_.sig.name, label), ffi::new_int64(*irseq as i64)))
            .collect();
        let _ = kv.set(&lpairs);
    }
}

/// 写用户声明的 rwir（无体）到 /lib/<opcode>。
pub fn write_rwir_decl(kv: &mut Kv, decl: &RwirDecl) {
    let mut opcode = decl.sig.name.clone();
    if !decl.pkg.is_empty() {
        opcode = format!("{}{}{opcode}", decl.pkg, keytree::MEMBER_SEP);
    }
    let v = kvkind::new_defrwir(
        decl.sig.num_reads(),
        decl.sig.num_writes(),
        &decl.sig.kindexp_list().join("\n"),
    );
    let _ = kv.set(&[(keytree::rwir(&opcode), v)]);
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
    let target_char = if s.writes.len() == 1 && !s.write_types.is_empty() && kvkind::is_char_kind(&s.write_types[0]) {
        s.write_types[0].as_str()
    } else {
        ""
    };

    let mut pairs: Vec<(String, Vec<u8>)> = Vec::with_capacity(1 + reads.len() + s.writes.len());
    if !opcode.is_empty() {
        pairs.push((format!("{prefix}/[{n},0]"), opcode_value(&opcode)));
    }
    for (j, r) in reads.iter().enumerate() {
        pairs.push((format!("{prefix}/[{n},-{}]", j + 1), slot_value(r, target_char)));
    }
    for (j, w) in s.writes.iter().enumerate() {
        pairs.push((format!("{prefix}/[{n},{}]", j + 1), slot_value(w, "")));
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
        || s == "null"
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
        assert_eq!(kvkind::kind(&kv.get_one("/lib/a/b·f/[0,0]")), "defrwfunc");

        // 同 lib a 下再 layout 另一嵌套 lib c，验证 b·f 未被整库删除（增量合并）
        compile(
            &mut kv,
            "lib a {\nlib c {\nrwfunc g() -> (r:int64) {\n2 -> r\n}\n}\n}\n",
        )
        .unwrap();
        assert_eq!(
            kvkind::kind(&kv.get_one("/lib/a/b·f/[0,0]")),
            "defrwfunc",
            "b·f 应保留"
        );
        assert_eq!(kvkind::kind(&kv.get_one("/lib/a/c·g/[0,0]")), "defrwfunc");
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
        assert_eq!(kvkind::kind(&kv.get_one("/lib/t·main/[0,0]")), "defrwfunc");
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

    #[test]
    fn frame_num_roundtrip() {
        let root = keytree::frame_at("7", 3);
        assert_eq!(root, "/vthread/7/[3]");
        assert_eq!(keytree::frame_num(&root), 3);
        let pc = keytree::entry_pc(&root);
        assert_eq!(pc, "/vthread/7/[3]/[1,0]");
        assert_eq!(keytree::frame_root(&pc), root);
        assert_eq!(keytree::frame_num(&pc), 3);
        assert_eq!(keytree::irseq_pc(&root, 5), "/vthread/7/[3]/[5,0]");
        assert!(keytree::is_entry_pc(&pc));
        assert!(!keytree::is_entry_pc(&keytree::irseq_pc(&root, 2)));
    }

    #[test]
    #[should_panic(expected = "expected [d]")]
    fn frame_num_rejects_old_nested_pc() {
        let _ = keytree::frame_num("/vthread/7/[3,0]/[1,0]");
    }

    #[test]
    #[should_panic(expected = "expected /[irseq,j]")]
    fn frame_num_rejects_scope_segment() {
        let _ = keytree::frame_num("/vthread/7/[1]/_else_1/[0,0]");
    }
}
