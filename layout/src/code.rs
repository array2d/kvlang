//! layoutcode（对齐 layout/layout.go 的编译期部分）：把 AST 写到 /lib/ 下的结构化 KV。
//!
//! 存储约定：
//!   /lib/<pkg>.<name>/[0,0]         编译后签名（kind=rwfunc）
//!   /lib/<pkg>.<name>/<param>       命名参数→slot 指针（kind=char, isptr=1）
//!   /lib/<pkg>.<name>/[i,j]         编译后指令（kind=rwir），i 从 1 开始
//!   /lib/<pkg>.<name>/<label>/      基本块子路径（scope 指令）
//!   /lib/<pkg>.<name>.src           源码副本

use std::collections::HashMap;

use super::ast::{Func, RwirDecl, Stmt};
use super::ffi::Kv;
use super::{builtin, ffi, keytree, kvkind, lower, parser, symbol};

/// 创建基础目录 /lib/ 与 /vthread/（layout 前必须存在）。
pub fn init_dirs(kv: &mut Kv) -> Result<(), String> {
    kv.mkindex("/lib/")?;
    kv.mkindex("/vthread/")?;
    Ok(())
}

/// 顶层入口（对齐 cmd/kvlang/layout.go 的 cmdLayout）：parse → lower → write。
pub fn compile(kv: &mut Kv, src: &str) -> Result<(), String> {
    let (file, diags) = parser::parse_code(src)?;
    for d in &diags {
        eprintln!("{}", d.string());
    }
    if parser::has_errors(&diags) {
        return Err("parse: error-level diagnostics — refusing to load".to_string());
    }

    let mut any_code = false;
    for func in &file.funcs {
        let pkg = if func.pkg.is_empty() { file.package.clone() } else { func.pkg.clone() };
        let mut lowered = lower::lower_func(func);
        write_func(kv, &pkg, &mut lowered);
        any_code = true;
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
            sig: super::ast::FuncSig { name: "init".to_string(), params: Vec::new(), returns: Vec::new() },
            body,
            pkg: String::new(),
        };
        let mut lowered = lower::lower_func(&init_fn);
        write_func(kv, "", &mut lowered);
        any_code = true;
    }

    if !any_code {
        return Err("no executable code found".to_string());
    }
    Ok(())
}

/// 校验源码是否可 layout（parse + lower），但不写入 kvspace。
/// 供运行时 vet 闸门：LLM 生成的 kv 代码先过此关，失败不污染 /lib。
pub fn vet(src: &str) -> Result<(), String> {
    let (file, diags) = parser::parse_code(src)?;
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

/// 写函数到 /lib/：签名（rwfunc）、源码、参数 Ptr、指令体。
pub fn write_func(kv: &mut Kv, pkg: &str, fn_: &mut Func) {
    let mut type_map = lower::infer_types(fn_);
    lower::specialize(fn_, &type_map);
    let func_dir = keytree::lib_func(pkg, &fn_.sig.name);

    let _ = kv.del_tree(&func_dir);
    let _ = kv.mkindex(&format!("{func_dir}/"));

    let nr = fn_.sig.num_reads();
    let nw = fn_.sig.num_writes();
    let param_types: Vec<String> = fn_.sig.kindexp_list();

    let mut pairs: Vec<(String, Vec<u8>)> = Vec::new();
    pairs.push((
        format!("{func_dir}/[0,0]"),
        kvkind::new_rwfunc(count_direct_insts(&fn_.body), nr, nw, &param_types),
    ));
    pairs.push((
        keytree::lib_src(pkg, &fn_.sig.name),
        ffi::new_char_byte(fn_.full_text().as_bytes()),
    ));
    for (i, p) in fn_.sig.params.iter().enumerate() {
        let slot = format!("[0,-{}]", i + 1);
        pairs.push((format!("{func_dir}/{}", p.name), ffi::new_ptr(kvkind::KIND_CHAR, &slot, 1)));
    }
    for (i, r) in fn_.sig.returns.iter().enumerate() {
        let slot = format!("[0,{}]", i + 1);
        pairs.push((format!("{func_dir}/{}", r.name), ffi::new_ptr(kvkind::KIND_CHAR, &slot, 1)));
    }
    let _ = kv.set(&pairs);

    write_body(kv, pkg, &fn_.sig.name, &fn_.body, &mut type_map, 1);
}

/// 写用户声明的 rwir（无体）到 /lib/<opcode>。
pub fn write_rwir_decl(kv: &mut Kv, decl: &RwirDecl) {
    let mut opcode = decl.sig.name.clone();
    if !decl.pkg.is_empty() {
        opcode = format!("{}{}{opcode}", decl.pkg, keytree::MEMBER_SEP);
    }
    let v = kvkind::new_rwir(decl.sig.num_reads(), decl.sig.num_writes(), &decl.sig.kindexp_list().join("\n"));
    let _ = kv.set(&[(keytree::rwir(&opcode), v)]);
}

/// 将 body 写入 /lib/<pkg>/<name>/ 下。offset 起始 idx（顶层函数=1）。
fn write_body(kv: &mut Kv, pkg: &str, name: &str, body: &[Stmt], type_map: &mut HashMap<String, String>, offset: i32) {
    let prefix = keytree::lib_func(pkg, name);
    let mut idx = offset;
    for st in body {
        write_stmt(kv, st, &prefix, &mut idx, type_map, pkg);
    }
}

fn write_stmt(
    kv: &mut Kv,
    st: &Stmt,
    prefix: &str,
    idx: &mut i32,
    type_map: &mut HashMap<String, String>,
    pkg: &str,
) {
    match st {
        Stmt::Instruction(s) => {
            let n = *idx;
            for (j, w) in s.writes.iter().enumerate() {
                if j < s.write_types.len() && !s.write_types[j].is_empty() {
                    type_map.insert(w.clone(), s.write_types[j].clone());
                }
            }
            let (mut opcode, reads) = s.flat();
            if !pkg.is_empty()
                && !builtin::is_native_rwir(&opcode)
                && !builtin::is_global_rwir(&opcode)
                && !is_control_op(&opcode)
                && !opcode.contains(keytree::MEMBER_SEP)
                && !opcode.starts_with("/lib/")
                && symbol::lookup(&opcode).word != "assign"
            {
                opcode = format!("{pkg}{}{opcode}", keytree::MEMBER_SEP);
            }
            let target_char = if s.writes.len() == 1 && !s.write_types.is_empty() && kvkind::is_char_kind(&s.write_types[0]) {
                s.write_types[0].as_str()
            } else {
                ""
            };

            let mut pairs: Vec<(String, Vec<u8>)> = Vec::with_capacity(1 + reads.len() + s.writes.len());
            if !opcode.is_empty() {
                pairs.push((format!("{prefix}/[{n},0]"), slot_value(&opcode, "")));
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
            *idx = n + 1;
        }
        Stmt::Scope(s) => {
            let scope_prefix = format!("{prefix}/{}", s.label);
            let mut scope_idx = 0;
            for child in &s.body {
                write_stmt_scope(kv, child, &scope_prefix, &mut scope_idx, type_map, pkg, prefix);
            }
        }
        _ => {}
    }
}

fn write_stmt_scope(
    kv: &mut Kv,
    st: &Stmt,
    scope_prefix: &str,
    idx: &mut i32,
    type_map: &mut HashMap<String, String>,
    pkg: &str,
    func_prefix: &str,
) {
    match st {
        Stmt::Instruction(s) => {
            let n = *idx;
            for (j, w) in s.writes.iter().enumerate() {
                if j < s.write_types.len() && !s.write_types[j].is_empty() {
                    type_map.insert(w.clone(), s.write_types[j].clone());
                }
            }
            let (mut opcode, reads) = s.flat();
            if !pkg.is_empty()
                && !builtin::is_native_rwir(&opcode)
                && !builtin::is_global_rwir(&opcode)
                && !is_control_op(&opcode)
                && !opcode.contains(keytree::MEMBER_SEP)
                && !opcode.starts_with("/lib/")
                && symbol::lookup(&opcode).word != "assign"
            {
                opcode = format!("{pkg}{}{opcode}", keytree::MEMBER_SEP);
            }
            let target_char = if s.writes.len() == 1 && !s.write_types.is_empty() && kvkind::is_char_kind(&s.write_types[0]) {
                s.write_types[0].as_str()
            } else {
                ""
            };

            let mut pairs: Vec<(String, Vec<u8>)> = Vec::with_capacity(1 + reads.len() + s.writes.len());
            if !opcode.is_empty() {
                pairs.push((format!("{scope_prefix}[{n},0]"), slot_value(&opcode, "")));
            }
            for (j, r) in reads.iter().enumerate() {
                pairs.push((format!("{scope_prefix}[{n},-{}]", j + 1), slot_value(r, target_char)));
            }
            for (j, w) in s.writes.iter().enumerate() {
                pairs.push((format!("{scope_prefix}[{n},{}]", j + 1), slot_value(w, "")));
            }
            if !pairs.is_empty() {
                let _ = kv.set(&pairs);
            }
            *idx = n + 1;
        }
        Stmt::Scope(s) => {
            let child_prefix = format!("{func_prefix}/{}", s.label);
            let mut child_idx = 0;
            for child in &s.body {
                write_stmt_scope(kv, child, &child_prefix, &mut child_idx, type_map, pkg, func_prefix);
            }
        }
        _ => {}
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
        let k = if target_char.is_empty() { kvkind::KIND_CHAR } else { target_char };
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

fn count_direct_insts(body: &[Stmt]) -> i32 {
    body.iter().filter(|st| matches!(st, Stmt::Instruction(_))).count() as i32
}

fn is_control_op(op: &str) -> bool {
    matches!(op, "call" | "return" | "br" | "goto")
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
