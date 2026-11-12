
use std::collections::{BTreeSet, HashMap, HashSet};

use super::ast::{self, Expr, Func, Instruction, LitKind, Stmt, MEMBER_READ_OP};
use super::scanner::{Diagnostic, Pos};
use super::{builtin, keytree, symbol};

pub fn deref_value_type(pointer_type: &str) -> &str {
    pointer_type.strip_prefix('*').unwrap_or(pointer_type)
}

pub fn normalize_slot_name(text: &str) -> (String, bool) {
    let original = text;
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
    let (name, dereferenced) = match current.strip_prefix('*') {
        Some(rest) => (rest, true),
        None => (current, false),
    };
    
    let mut characters = name.chars();
    let identifier_ok = match characters.next() {
        Some(first) if first == '_' || first.is_alphabetic() => {
            characters.all(|c| c == '_' || c.is_alphanumeric())
        }
        _ => false,
    };
    if !identifier_ok {
        return (original.to_string(), false);
    }
    (name.to_string(), dereferenced)
}

fn char_encoding_of(langtype: &str) -> Option<String> {
    let mut text = langtype.trim();
    while let Some(rest) = text.strip_prefix('*') {
        text = rest;
    }
    
    if let Some(rest) = text.strip_prefix('[') {
        let end = rest.find(']')?;
        text = &rest[end + 1..];
    }
    text.strip_prefix("char/").map(str::to_string)
}

fn concat_operand_type(langtype: &str) -> Option<String> {
    let text = langtype.trim();
    if text.is_empty() {
        None
    } else {
        Some(text.to_string())
    }
}

fn concat_result_encoding(operand_types: &[String]) -> Option<String> {
    let mut encoding: Option<String> = None;
    for langtype in operand_types {
        let this = char_encoding_of(langtype)?;
        if this == "ascii" {
            continue;
        }
        match &encoding {
            None => encoding = Some(this),
            Some(known) if *known == this => {}
            Some(_) => return None,
        }
    }
    
    Some(match encoding {
        None => format!("[]{}", super::kvkind::KIND_CHAR),
        Some(known) => format!("[]char/{known}"),
    })
}

fn is_string_kind(t: &str) -> bool {
    t.contains("char")
}

pub fn is_none_comparable(t: &str) -> bool {
    
    !t.is_empty() && !t.starts_with('@')
}

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
    if super::kvkind::is_map_langtype(&ar)
        || ar == "stringkeymap"
        || super::kvkind::is_map_langtype(&vr)
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
    
    t == "stringkeymap" || super::kvkind::is_map_langtype(t)
}

fn is_container_ty(ty: &str) -> bool {
    
    let t = ty.trim_start_matches(['*', '@']);
    super::kvkind::is_map_langtype(t) || t.starts_with('/')
}

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
    
    if ann.starts_with('/') {
        return;
    }
    
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
        
        format!("container literal `{{…}}` on {target:?} needs a map langtype, e.g. `{target}:'[]char/utf8'int64 = {{}}`")
    } else if !super::langtype::valid_langtype(declared) {
        format!("invalid langtype {declared:?} on container literal target {target:?}")
    } else if !super::kvkind::is_map_langtype(declared) {
        
        format!("container type {declared:?} is not a map langtype; expect `'{{memitemkeylangtype}}'{{memitemvaluelangtype}}`")
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

fn collect_container_decls(body: &[Stmt], out: &mut HashSet<String>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => {
                
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
    
    let base_name = base
        .strip_prefix("(*")
        .and_then(|r| r.strip_suffix(')'))
        .unwrap_or(base);
    
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

pub fn check_container_subscript(fn_: &Func, structs: &StructTable) -> Vec<Diagnostic> {
    let tm = infer_types(fn_, structs);
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

struct LoopCtx {
    break_label: String,
    continue_label: String,
}

struct LabelGen {
    n: i32,
    
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

pub fn annotate_position_slot_types(fn_: &mut Func, tm: &HashMap<String, String>) {
    annotate_position_slots_in(&mut fn_.body, tm);
}

fn annotate_position_slots_in(body: &mut [Stmt], tm: &HashMap<String, String>) {
    for st in body.iter_mut() {
        match st {
            Stmt::Scope(s) => annotate_position_slots_in(&mut s.body, tm),
            Stmt::If(s) => {
                annotate_position_slots_in(&mut s.then_, tm);
                annotate_position_slots_in(&mut s.else_, tm);
            }
            Stmt::While(s) => annotate_position_slots_in(&mut s.body, tm),
            Stmt::For(s) => annotate_position_slots_in(&mut s.body, tm),
            _ => {}
        }
    }
    
    let positions: Vec<usize> = body
        .iter()
        .enumerate()
        .filter(|(_, st)| {
            matches!(st, Stmt::Instruction(inst)
                if inst.expr.as_ref().is_some_and(|e| e.op == "kvlang·resolvekey"))
        })
        .map(|(index, _)| index)
        .collect();
    for index in positions {
        let Some(next) = body.get(index + 1) else {
            continue;
        };
        let Stmt::Instruction(write_inst) = next else {
            continue;
        };
        if write_inst.writes.len() != 1
            || write_inst
                .write_types
                .first()
                .is_some_and(|t| !t.is_empty())
        {
            continue;
        }
        let Some(slot) = write_inst.writes[0].strip_prefix('*') else {
            continue;
        };
        if !(slot.starts_with("_p") && slot[2..].chars().all(|c| c.is_ascii_digit())) {
            continue;
        }
        
        let base_name = match &body[index] {
            Stmt::Instruction(position_inst) => position_inst
                .expr
                .as_ref()
                .and_then(|expr| expr.args.first())
                .map(|a| a.val.clone())
                .unwrap_or_default(),
            _ => String::new(),
        };
        let base_type = arg_type(&ast::leaf(&base_name), tm);
        if !base_is_path_string(&base_type) {
            continue;
        }
        let Some(value_type) = position_slot_value_type(write_inst) else {
            continue;
        };
        let Stmt::Instruction(position_inst) = &mut body[index] else {
            continue;
        };
        if position_inst
            .write_types
            .first()
            .is_none_or(|t| t.is_empty())
        {
            
            position_inst.write_types = vec![format!("*{value_type}")];
        }
        
        let Stmt::Instruction(write_mut) = &mut body[index + 1] else {
            continue;
        };
        if write_mut.write_types.first().is_none_or(|t| t.is_empty()) {
            write_mut.write_types = vec![value_type];
        }
    }
    
    let read_positions: Vec<usize> = body
        .iter()
        .enumerate()
        .filter(|(_, st)| {
            matches!(st, Stmt::Instruction(inst)
                if inst.expr.as_ref().is_some_and(|e| e.op == "kvlang·resolvekey")
                    && inst.write_types.first().is_none_or(|t| t.is_empty()))
        })
        .map(|(index, _)| index)
        .collect();
    for index in read_positions {
        let base_name = match &body[index] {
            Stmt::Instruction(position_inst) => position_inst
                .expr
                .as_ref()
                .and_then(|expr| expr.args.first())
                .map(|a| a.val.clone())
                .unwrap_or_default(),
            _ => String::new(),
        };
        if !base_is_path_string(&arg_type(&ast::leaf(&base_name), tm)) {
            continue;
        }
        
        let produced = match &body[index] {
            Stmt::Instruction(position_inst) => {
                position_inst.writes.first().cloned().unwrap_or_default()
            }
            _ => String::new(),
        };
        let deref_type = match body.get(index + 1) {
            Some(Stmt::Instruction(use_inst)) => {
                let source = use_inst
                    .expr
                    .as_ref()
                    .filter(|expr| expr.op == ast::DEREF_OP)
                    .and_then(|expr| expr.args.first())
                    .map(|a| a.val.clone());
                let target_type = use_inst
                    .write_types
                    .first()
                    .filter(|t| !t.is_empty())
                    .cloned();
                match (source, target_type) {
                    (Some(source), Some(target))
                        if source == produced && produced.starts_with('_') =>
                    {
                        Some(format!("*{target}"))
                    }
                    _ => None,
                }
            }
            _ => None,
        };
        if let (Some(position_type), Stmt::Instruction(position_mut)) =
            (deref_type, &mut body[index])
        {
            position_mut.write_types = vec![position_type];
        }
    }
}

fn position_slot_value_type(inst: &Instruction) -> Option<String> {
    let e = inst.expr.as_ref()?;
    
    let t = arg_type(e, &HashMap::new());
    (!t.is_empty()).then_some(t)
}

pub fn declared_annotated_names(fn_: &Func) -> std::collections::HashSet<String> {
    let mut names = std::collections::HashSet::new();
    collect_annotated(&fn_.body, &mut names);
    names
}

fn collect_annotated(body: &[Stmt], names: &mut std::collections::HashSet<String>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => {
                for (j, w) in s.writes.iter().enumerate() {
                    if j < s.write_types.len()
                        && !s.write_types[j].is_empty()
                        && slot_is_plain_name(w)
                    {
                        names.insert(w.clone());
                    }
                }
            }
            Stmt::Scope(s) => collect_annotated(&s.body, names),
            Stmt::If(s) => {
                collect_annotated(&s.then_, names);
                collect_annotated(&s.else_, names);
            }
            Stmt::While(s) => collect_annotated(&s.body, names),
            Stmt::For(s) => collect_annotated(&s.body, names),
            _ => {}
        }
    }
}

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
        
        let full = ret.pointer_type();
        let value_type = deref_value_type(&full);
        if inst.write_types[j].is_empty() && !value_type.is_empty() {
            inst.write_types[j] = value_type.to_string();
        }
    }
}

const NATIVE_RWIR_DECLS: &str = include_str!("../../runtimec/specification/native_rwir_decls.kv");

fn native_rwir_signatures() -> &'static HashMap<String, ast::FuncSig> {
    static TABLE: std::sync::OnceLock<HashMap<String, ast::FuncSig>> = std::sync::OnceLock::new();
    TABLE.get_or_init(|| {
        
        let (file, diags) = crate::parser::parse_code(NATIVE_RWIR_DECLS)
            .unwrap_or_else(|e| panic!("native_rwir_decls.kv: parse failed: {e}"));
        
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

pub fn sig_lookup<'a>(sigs: &'a SigTable, op: &str) -> Option<&'a ast::FuncSig> {
    let key = op.strip_prefix("/lib/").unwrap_or(op);
    sigs.get(key).or_else(|| native_rwir_signatures().get(key))
}

pub fn native_rwir_sig(name: &str) -> Option<&'static ast::FuncSig> {
    native_rwir_signatures().get(name)
}

const NATIVE_CATALOG: &str = include_str!("../../runtimec/specification/myrwircaps.c");

pub fn native_catalog_has(name: &str) -> bool {
    static NAMES: std::sync::OnceLock<HashSet<String>> = std::sync::OnceLock::new();
    NAMES
        .get_or_init(|| {
            
            NATIVE_CATALOG
                .lines()
                .filter_map(|line| line.trim().strip_prefix("{\""))
                .filter_map(|rest| rest.split_once("\", RUNTIMEC_MYRWIRCAPS_ORIGIN_"))
                .map(|(name, _)| name.to_string())
                .collect()
        })
        .contains(name)
}

fn is_fixed_width_array(ty: &str) -> bool {
    let t = ty.trim_start_matches('*');
    let Some(rest) = t.strip_prefix('[') else {
        return false;
    };
    let Some(close) = rest.find(']') else {
        return false;
    };
    
    let dimensions = &rest[..close];
    !dimensions.is_empty() && dimensions.bytes().all(|b| b.is_ascii_digit() || b == b',')
}

fn desugar_string_subscript(body: &mut [Stmt], tm: &HashMap<String, String>) {
    for st in body {
        match st {
            Stmt::Instruction(s) => desugar_string_subscript_inst(s, tm),
            Stmt::Scope(s) => desugar_string_subscript(&mut s.body, tm),
            Stmt::If(s) => {
                if let Some(c) = &mut s.cond {
                    desugar_string_subscript_inst(c, tm);
                }
                desugar_string_subscript(&mut s.then_, tm);
                desugar_string_subscript(&mut s.else_, tm);
            }
            Stmt::While(s) => {
                if let Some(c) = &mut s.cond {
                    desugar_string_subscript_inst(c, tm);
                }
                desugar_string_subscript(&mut s.body, tm);
            }
            Stmt::For(s) => desugar_string_subscript(&mut s.body, tm),
            _ => {}
        }
    }
}

fn desugar_string_subscript_inst(inst: &mut Instruction, tm: &HashMap<String, String>) {
    let Some(e) = inst.expr.as_mut() else { return };
    desugar_string_subscript_expr(e, tm);
}

fn desugar_string_subscript_expr(e: &mut Expr, tm: &HashMap<String, String>) {
    
    for argument in e.args.iter_mut() {
        desugar_string_subscript_expr(argument, tm);
    }
    if e.op != "compactarray·at" || e.args.len() != 2 {
        return;
    }
    let Some(base) = e.args.first() else { return };
    if !base.is_leaf() || base.val.is_empty() {
        return;
    }
    let Some(base_type) = tm.get(&base.val) else {
        return;
    };
    
    if is_fixed_width_array(base_type) || !is_string_kind(base_type) {
        return;
    }
    e.op = "string·char".to_string();
}

pub fn lower_func(fn_: &Func, structs: &StructTable) -> Result<Func, String> {
    let tm = infer_types(fn_, structs);
    
    let mut fn_ = fn_.clone();
    desugar_string_subscript(&mut fn_.body, &tm);
    let fn_ = &fn_;
    let mut lg = LabelGen { n: 0, err: None };
    
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
        pos: Pos::default(),
    })
}

fn terminate(mut body: Vec<Stmt>) -> Vec<Stmt> {
    for st in &mut body {
        if let Stmt::Scope(s) = st {
            
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
    adjacent: bool,
) -> Vec<Stmt> {
    if stmts.is_empty() {
        return Vec::new();
    }
    let mut preamble: Vec<Stmt> = Vec::new();
    for (i, st) in stmts.iter().enumerate() {
        match st {
            Stmt::Instruction(s) => {
                
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
                
                let enters_directly = preamble.is_empty() && adjacent && targets.contains(&s.label);
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

    let then_ = inject_goto(
        lower_body(&s.then_, lg, lc, tm, targets, false),
        &merge_label,
    );
    let else_ = inject_goto(
        lower_body(&s.else_, lg, lc, tm, targets, false),
        &merge_label,
    );
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
    
    let is_container = s.iter.op == "map·new"
        || (s.iter.is_leaf()
            && tm
                .get(&s.iter.val)
                .is_some_and(|t| super::kvkind::is_map_langtype(t)));

    let mut init_body = Vec::new();
    let iter_slot = if s.iter.is_leaf() {
        s.iter.val.clone()
    } else if s.iter.op == "map·new" {
        let slot = lg.tmp();
        
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
            pos: Pos::default(),
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
            pos: Pos::default(),
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
            pos: Pos::default(),
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
        pos: Pos::default(),
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
        pos: Pos::default(),
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
            pos: Pos::default(),
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
            pos: Pos::default(),
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
            pos: Pos::default(),
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

fn is_none_comparison(inst: &Instruction) -> bool {
    let Some(e) = inst.expr.as_ref() else {
        return false;
    };
    if e.op != "==" && e.op != "!=" {
        return false;
    }
    e.args.iter().any(|a| a.is_leaf() && a.val == "None")
}

fn flatten_nested_calls(inst: &Instruction, lg: &mut LabelGen) -> (Instruction, Vec<Stmt>) {
    let mut extra = Vec::new();
    let mut flat = inst.clone();
    let none_test = is_none_comparison(inst);
    flat.expr = inst
        .expr
        .as_ref()
        .map(|e| flatten_expr_with(e, lg, &mut extra, none_test));
    (flat, extra)
}

fn flatten_expr_with(e: &Expr, lg: &mut LabelGen, extra: &mut Vec<Stmt>, none_test: bool) -> Expr {
    if e.is_leaf() {
        return e.clone();
    }
    
    if none_test && e.op == ast::DEREF_OP {
        if let Some(inner) = e.args.first() {
            if inner.op == "kvlang·resolvekey" {
                
                let position_slot = lg.tmp();
                extra.push(Stmt::Instruction(Instruction {
                    comments: Vec::new(),
                    expr: Some(inner.clone()),
                    writes: vec![position_slot.clone()],
                    write_types: Vec::new(),
                    arrow_left: false,
                    operator: false,
                    operand_kind: String::new(),
                    pos: Pos::default(),
                }));
                return ast::leaf(&format!("*{position_slot}"));
            }
        }
    }
    let mut new_args = Vec::with_capacity(e.args.len());
    for arg in &e.args {
        if !arg.is_leaf() {
            let flat_expr = flatten_expr_with(arg, lg, extra, none_test);
            
            if none_test && flat_expr.is_leaf() && flat_expr.val.starts_with(ast::DEREF_OP) {
                new_args.push(flat_expr);
                continue;
            }
            let tmp = lg.tmp();
            
            let prototype = if flat_expr.op == format!("struct{}new", keytree::MEMBER_SEP) {
                flat_expr
                    .args
                    .first()
                    .filter(|a| a.quote != 0)
                    .map(|a| a.val.clone())
                    .unwrap_or_default()
            } else {
                String::new()
            };
            extra.push(Stmt::Instruction(Instruction {
                comments: Vec::new(),
                expr: Some(flat_expr),
                writes: vec![tmp.clone()],
                write_types: if prototype.is_empty() {
                    Vec::new()
                } else {
                    vec![prototype]
                },
                arrow_left: false,
                operator: false,
                operand_kind: String::new(),
                pos: Pos::default(),
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
        pos: Pos::default(),
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
        pos: Pos::default(),
    })
}

fn infer_sparse_type(e: &Expr, tm: &HashMap<String, String>) -> Vec<String> {
    let (keylt, val) = if e.op == "map·new" {
        ("[int64]", e.args.first())
    } else {
        ("[]char/utf8", e.args.get(1))
    };
    
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

fn expand_sparse(base: &str, e: &Expr, wtypes: &[String], _lg: &mut LabelGen) -> Vec<Stmt> {
    let inst = Instruction {
        comments: Vec::new(),
        expr: Some(ast::call("map·new", e.args.clone())),
        writes: vec![base.to_string()],
        
        write_types: wtypes.to_vec(),
        arrow_left: true,
        operator: false,
        operand_kind: String::new(),
        pos: Pos::default(),
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
        pos: Pos::default(),
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

pub struct SourceLangtypes {
    entries: HashMap<String, String>,
}

impl SourceLangtypes {
    pub fn new(sig: &ast::FuncSig) -> Self {
        let mut entries = HashMap::new();
        for parameter in sig.params.iter().chain(sig.returns.iter()) {
            
            if !parameter.ty.is_empty() && !parameter.name.is_empty() {
                entries.insert(parameter.name.clone(), parameter.declared_type());
            }
        }
        Self { entries }
    }

    pub fn merged_under(&self, declared: &HashMap<String, String>) -> HashMap<String, String> {
        let mut merged = self.entries.clone();
        for (name, ty) in declared {
            merged.insert(name.clone(), ty.clone());
        }
        merged
    }

    pub fn langtype_value(&self, name: &str) -> Option<String> {
        let source = self.entries.get(name)?;
        let by_ref_len = source.len() - source.trim_start_matches(['*', '@']).len();
        let (by_ref, ty) = source.split_at(by_ref_len);
        if ty.starts_with('/') || ty.starts_with('*') {
            Some(source.clone())
        } else if by_ref.is_empty() {
            Some(ty.to_string())
        } else {
            Some(ty.to_string())
        }
    }

    pub fn langtype_wire(&self, name: &str) -> Option<String> {
        let source = self.entries.get(name)?;
        Some(
            source
                .trim_end_matches("...")
                .trim_start_matches(['*', '@'])
                .to_string(),
        )
    }
}

pub fn value_langtypes(fn_: &Func) -> HashMap<String, String> {
    
    let source = SourceLangtypes::new(&fn_.sig);
    let mut d = HashMap::new();
    for p in fn_.sig.params.iter().chain(fn_.sig.returns.iter()) {
        if !p.ty.is_empty() && !p.name.is_empty() {
            if let Some(value) = source.langtype_value(&p.name) {
                d.insert(p.name.clone(), value);
            }
        }
    }
    d
}

pub fn source_level_langtypes(fn_: &Func) -> HashMap<String, String> {
    let mut d = HashMap::new();
    collect_declared(&fn_.body, &mut d);
    
    d
}

pub fn declared_langtypes_by_origin(fn_: &Func) -> HashMap<String, String> {
    let mut declared = source_level_langtypes(fn_);
    for (name, type_text) in value_langtypes(fn_) {
        declared.insert(name, type_text);
    }
    declared
}

pub fn infer_types(fn_: &Func, structs: &StructTable) -> HashMap<String, String> {
    
    let mut tm = value_langtypes(fn_);
    infer_body(&fn_.body, &mut tm, structs);
    tm
}

fn infer_body(body: &[Stmt], tm: &mut HashMap<String, String>, structs: &StructTable) {
    for st in body {
        match st {
            Stmt::Instruction(s) => infer_inst(s, tm, structs),
            Stmt::Scope(s) => infer_body(&s.body, tm, structs),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    infer_inst(c, tm, structs);
                }
                infer_body(&s.then_, tm, structs);
                infer_body(&s.else_, tm, structs);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    infer_inst(c, tm, structs);
                }
                infer_body(&s.body, tm, structs);
            }
            Stmt::For(s) => infer_body(&s.body, tm, structs),
            _ => {}
        }
    }
}

fn per_write_types(
    inst: &Instruction,
    tm: &mut HashMap<String, String>,
    structs: &StructTable,
    output_type_failure: &mut Option<String>,
) -> Vec<Option<String>> {
    let Some(e) = &inst.expr else {
        return Vec::new();
    };
    let shared = if e.is_leaf() && e.lit != LitKind::LitNone {
        lit_to_type(e.lit).to_string()
    } else {
        
        let (opcode, reads) = inst.flat();
        if opcode.is_empty() {
            String::new()
        } else {
            let annotated = inst.write_types.first().map(String::as_str).unwrap_or("");
            infer_op_type(&opcode, &reads, tm, annotated, structs, output_type_failure)
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

fn infer_inst(inst: &Instruction, tm: &mut HashMap<String, String>, structs: &StructTable) {
    if inst.writes.is_empty() {
        return;
    }
    let mut ignored_conflict = None;
    let types = per_write_types(inst, tm, structs, &mut ignored_conflict);
    for (w, t) in inst.writes.iter().zip(types) {
        if w.is_empty() || w.starts_with(keytree::MEMBER_SEP) {
            continue;
        }
        if let Some(t) = t {
            tm.entry(w.clone()).or_insert(t);
        }
    }
}

pub fn infer_type_sets(fn_: &Func, structs: &StructTable) -> HashMap<String, Vec<String>> {
    let mut base_sets: HashMap<String, BTreeSet<String>> = HashMap::new();
    let mut base_work: HashMap<String, String> = HashMap::new();
    for p in fn_.sig.params.iter().chain(fn_.sig.returns.iter()) {
        
        if p.name.is_empty() {
            continue;
        }
        insert_type_set(&mut base_sets, &p.name, &p.declared_type());
        if !p.ty.is_empty() {
            base_work.insert(p.name.clone(), p.declared_type());
        }
    }
    let mut declared = HashMap::new();
    collect_declared(&fn_.body, &mut declared);
    
    for (name, ty) in &declared {
        insert_type_set(&mut base_sets, name, ty);
        if !name.is_empty() && !ty.is_empty() {
            base_work.insert(name.clone(), ty.clone());
        }
    }
    let mut work = base_work;
    let mut sets = base_sets.clone();
    let mut unknown: HashSet<String> = HashSet::new();
    
    for _ in 0..4 {
        let mut next_sets = base_sets.clone();
        let mut next_unknown: HashSet<String> = HashSet::new();
        collect_type_sets(
            &fn_.body,
            &mut next_sets,
            &mut next_unknown,
            &mut work,
            structs,
        );
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
    
    for name in unknown {
        if !declared.contains_key(&name) {
            sets.remove(&name);
        }
    }
    sets.into_iter()
        .map(|(name, set)| (name, set.into_iter().collect()))
        .collect()
}

fn insert_type_set(sets: &mut HashMap<String, BTreeSet<String>>, name: &str, ty: &str) {
    if name.is_empty() || ty.is_empty() {
        return;
    }
    for part in ty.split('|') {
        let part = part.trim();
        if !part.is_empty() {
            sets.entry(name.to_string())
                .or_default()
                .insert(part.to_string());
        }
    }
}

fn collect_type_sets(
    body: &[Stmt],
    sets: &mut HashMap<String, BTreeSet<String>>,
    unknown: &mut HashSet<String>,
    tm: &mut HashMap<String, String>,
    structs: &StructTable,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => {
                record_inst_sets(s, sets, unknown, tm, structs);
            }
            Stmt::Scope(s) => collect_type_sets(&s.body, sets, unknown, tm, structs),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    record_inst_sets(c, sets, unknown, tm, structs);
                }
                collect_type_sets(&s.then_, sets, unknown, tm, structs);
                collect_type_sets(&s.else_, sets, unknown, tm, structs);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    record_inst_sets(c, sets, unknown, tm, structs);
                }
                collect_type_sets(&s.body, sets, unknown, tm, structs);
            }
            Stmt::For(s) => collect_type_sets(&s.body, sets, unknown, tm, structs),
            _ => {}
        }
    }
}

fn record_inst_sets(
    s: &Instruction,
    sets: &mut HashMap<String, BTreeSet<String>>,
    unknown: &mut HashSet<String>,
    tm: &mut HashMap<String, String>,
    structs: &StructTable,
) {
    let (op, reads) = s.flat();
    
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
    
    let nullable_lookup = (symbol::lookup(&op).word == "assign" && reads.len() > 1)
        || matches!(op.as_str(), "scatterarray·at" | "kvspace·get");
    let mut concat_conflict = None;
    let types = per_write_types(s, tm, structs, &mut concat_conflict);
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

pub fn type_members(t: &str) -> Vec<&str> {
    t.split('|')
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .collect()
}

fn join_type_members(set: BTreeSet<String>) -> String {
    set.into_iter().collect::<Vec<_>>().join("|")
}

pub fn literal_slot_type(name: &str) -> String {
    slot_type(name, &HashMap::new())
}

pub fn type_set_slot_type(sets: &HashMap<String, Vec<String>>, name: &str) -> Option<String> {
    let types = sets.get(name)?;
    let members: BTreeSet<String> = types.iter().cloned().collect();
    if members.is_empty() {
        None
    } else {
        Some(join_type_members(members))
    }
}

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

pub fn is_cast_op(op: &str) -> bool {
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

pub fn exact_numeric_kind(
    e: &Expr,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
) -> Option<String> {
    let kind = exact_kind(e, declared, stable)?;
    builtin::is_num_kind(&kind).then_some(kind)
}

pub fn wrap_cast(expr: &Expr, kind: &str) -> Expr {
    if !expr.is_leaf() && expr.op == kind {
        return expr.clone();
    }
    ast::call(kind, vec![expr.clone()])
}

fn infer_op_type(
    opcode: &str,
    reads: &[String],
    tm: &mut HashMap<String, String>,
    annotated: &str,
    structs: &StructTable,
    output_type_failure: &mut Option<String>,
) -> String {
    if matches!(opcode, "return" | "goto" | "br" | "call") {
        return String::new();
    }
    
    if matches!(opcode, "not" | "and" | "or") {
        return "bool".to_string();
    }
    if symbol::lookup(opcode).word == "assign" {
        
        if let Some(first) = reads.first() {
            if let Some((base, tail)) = first.split_once(keytree::MEMBER_SEP) {
                let base_type = slot_type(base, tm);
                return match member_chain_type(&base_type, tail, structs) {
                    Some(t) => t,
                    None => {
                        
                        *output_type_failure = Some(format!(
                            "成员读 `{first}` 的类型推不出：基座 `{base}` 的类型 `{base_type}` 无法沿 `{tail}` 逐段下钻"
                        ));
                        String::new()
                    }
                };
            }
        }
        if reads.len() > 1 {
            
            let mut ty = slot_type(&reads[0], tm);
            for part in &reads[1..] {
                if let Some(v) = container_value(&ty) {
                    ty = v;
                } else if ty.starts_with('[') {
                    ty = container_elem(&ty).unwrap_or_default();
                } else if let Some(field) = struct_field_type(&ty, part, structs) {
                    ty = field;
                } else {
                    return String::new();
                }
            }
            return ty;
        }
        
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
        
        for r in reads {
            let t = slot_type(r, tm);
            let members = type_members(&t);
            if (!members.is_empty() && members.iter().all(|m| is_string_kind(m)))
                || is_string_kind(&t)
            {
                
                let operand_types: Vec<String> = reads
                    .iter()
                    .filter_map(|read| concat_operand_type(&slot_type(read, tm)))
                    .collect();
                return match concat_result_encoding(&operand_types) {
                    Some(t) => t,
                    None => {
                        
                        *output_type_failure = Some(format!(
                            "concat 的结果类型无法确定：操作数编码 {} 没有共同编码",
                            operand_types
                                .iter()
                                .map(|t| format!("`{t}`"))
                                .collect::<Vec<_>>()
                                .join(" 与 ")
                        ));
                        String::new()
                    }
                };
            }
        }
        let word = symbol::lookup(opcode).word.to_string();
        
        if word == "sub" && reads.len() == 1 {
            return "int64".to_string();
        }
        
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
    if opcode == "kvspace/key·abs" {
        
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
        "kvlen"
        | "compactarray·numel"
        | "compactarray·dim"
        | "kvspace·listlen"
        | "string·len"
        | "string·ord"
        | "string·cmp"
        | "string·find"
        | "string·parseint"
        | "compactarray·bodylen"
        | "scatterarray·len" => {
            return "int64".to_string();
        }
        "compactarray·langtype" => return "[]char/utf8".to_string(),
        "compactarray·shape" => return "[]int64".to_string(),
        
        "kvlang·resolvekey" => {
            
            let base = reads.first().map(|r| slot_type(r, tm)).unwrap_or_default();
            let container = deref_value_type(&base);
            
            if base_is_path_string(container) {
                return String::new();
            }
            let member = container_value(container)
                .or_else(|| container_elem(container))
                .unwrap_or_default();
            if member.is_empty() {
                return String::new();
            }
            return format!("*{member}");
        }
        "string·char" | "string·set" | "string·slice" | "string·formatint"
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

fn arg_type(e: &Expr, tm: &HashMap<String, String>) -> String {
    
    if e.quote != 0 || matches!(e.lit, LitKind::LitString | LitKind::LitRawString) {
        return lit_to_type(LitKind::LitString).to_string();
    }
    match e.lit {
        LitKind::LitInt => "int64".to_string(),
        LitKind::LitFloat => "float64".to_string(),
        LitKind::LitBool => "bool".to_string(),
        _ => {
            if e.op == ast::MEMBER_READ_OP {
                
                let base = e.args.first().map(|a| arg_type(a, tm)).unwrap_or_default();
                if base.is_empty() {
                    return String::new();
                }
                let tail: Vec<&str> = e.args.iter().skip(1).map(|a| a.val.as_str()).collect();
                return member_chain_type(
                    &base,
                    &tail.join(&keytree::MEMBER_SEP.to_string()),
                    &StructTable::default(),
                )
                .unwrap_or_default();
            }
            
            if e.op == ast::DEREF_OP {
                let inner = e
                    .args
                    .first()
                    .map(|a| slot_type(&a.val, tm))
                    .unwrap_or_default();
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
    
    {
        let (bare, dereferenced) = normalize_slot_name(name);
        if dereferenced {
            if let Some(t) = tm.get(&bare) {
                return deref_value_type(t).to_string();
            }
        }
    }
    if let Some(t) = tm.get(name) {
        return t.clone();
    }
    
    if name.starts_with('"') && !name.contains(keytree::MEMBER_SEP) {
        
        return lit_to_type(LitKind::LitString).to_string();
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
        LitKind::LitString | LitKind::LitRawString => "[]char/utf32",
        LitKind::LitBool => "bool",
        _ => "",
    }
}

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
    let mut saw_bool = false;
    for a in args {
        let t = arg_type(a, tm);
        
        if t.is_empty() {
            continue;
        }
        
        let members = type_members(&t);
        let all_num = !members.is_empty() && members.iter().all(|m| builtin::is_num_kind(m));
        let all_str = !members.is_empty() && members.iter().all(|m| is_string_kind(m));
        let all_bool = !members.is_empty() && members.iter().all(|m| m.trim() == "bool");
        
        let nullable = is_none_comparable;
        let all_cmp_ok = !members.is_empty() && members.iter().all(|m| nullable(m));
        
        let is_equality = word == "eq" || word == "neq";
        if is_string_kind(&t) || all_str {
            saw_char = true;
        } else if builtin::is_num_kind(&t) || all_num {
            saw_num = true;
        } else if is_equality && (t.trim() == "bool" || all_bool) {
            saw_bool = true;
        } else if is_cmp && all_cmp_ok {
            
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
    
    if saw_bool && (saw_num || saw_char) {
        push_num_diag(
            diags,
            format!("TypeError: {word} cannot mix bool with other kinds"),
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

fn exact_kind(
    e: &Expr,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
) -> Option<String> {
    if e.is_leaf() {
        if e.quote != 0 || matches!(e.lit, LitKind::LitString | LitKind::LitRawString) {
            
            return Some(lit_to_type(LitKind::LitString).to_string());
        }
        return match e.lit {
            LitKind::LitInt => Some("int64".to_string()),
            LitKind::LitFloat => Some("float64".to_string()),
            LitKind::LitBool => Some("bool".to_string()),
            LitKind::LitNone if e.val == "None" => None,
            _ => {
                
                if let Some(t) = literal_text_kind(&e.val) {
                    return Some(t.to_string());
                }
                
                let name = &e.val;
                if name.is_empty() {
                    None
                } else {
                    
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
        
        let inner = e
            .args
            .first()
            .and_then(|a| exact_kind(a, declared, stable))?;
        return Some(deref_value_type(&inner).to_string());
    }
    if op == "kvspace/key·abs" {
        
        let inner = e
            .args
            .first()
            .and_then(|a| exact_kind(a, declared, stable))?;
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
        
        let base = e.args.first()?;
        let bt = exact_kind(base, declared, stable)?;
        let mut ty = bt;
        for _ in e.args.iter().skip(1) {
            ty = container_value(&ty)?;
        }
        return Some(ty);
    }
    match word.as_str() {
        
        "compactarray·at"
        | "scatterarray·at"
        | "scatterarray·newcompact"
        | "compactarray·newreshape" => {
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
        "kvlen"
        | "compactarray·numel"
        | "compactarray·dim"
        | "kvspace·listlen"
        | "string·len"
        | "string·ord"
        | "string·cmp"
        | "string·find"
        | "string·parseint"
        | "compactarray·bodylen"
        | "scatterarray·len"
        | "random.int63" => return Some("int64".to_string()),
        "random·intn" | "random.uint64" | "string·parseuint" => {
            return Some("uint64".to_string())
        }
        "string·parsefloat" => return Some("float64".to_string()),
        
        "string·concat" => {
            
            let operand_types: Vec<String> = e
                .args
                .iter()
                .map(|a| match exact_kind(a, declared, stable) {
                    Some(t) if !t.is_empty() => t,
                    
                    _ => lit_to_type(a.lit).to_string(),
                })
                .collect();
            return concat_result_encoding(&operand_types);
        }
        "string·char" | "string·set" | "string·slice" | "string·formatint"
        | "string·formatuint" => return Some("char/utf32".to_string()),
        _ => {}
    }
    
    let arith = entry.arith;
    let maxmin = matches!(word.as_str(), "max" | "min");
    if !arith && !maxmin {
        return None;
    }
    if word == "sub" && e.args.len() == 1 {
        
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
                        es.iter()
                            .all(|e| exact_kind(e, declared, &found).as_deref() == Some(k.as_str()))
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
    if e.op == "kvspace/key·abs" {
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

pub fn exact_operand_kind(
    e: &Expr,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
) -> Option<String> {
    exact_kind(e, declared, stable)
}

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
    
    let mut kinds: Vec<String> = Vec::new();
    let mut literals: Vec<Option<&str>> = Vec::new();
    let mut string_operands = 0usize;
    for arg in &e.args {
        let t = match exact_operand_kind(arg, declared, stable) {
            Some(t) => t,
            None => {
                
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
    
    if string_operands > 0 {
        if word != "add" {
            
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
    
    let mut k = match promoted_numeric_kind(&kinds, !is_cmp, &literals) {
        Some(k) => k,
        None => return,
    };
    
    if word == "sub" && e.args.len() == 1 {
        if builtin::is_unsigned_kind(&k) {
            return;
        }
        e.op = "neg".to_string();
        inst.operator = true;
        inst.operand_kind = k;
        return;
    }
    
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
        
        if k == "float32" {
            return;
        }
        k = "float64".to_string();
    } else if matches!(word.as_str(), "bitand" | "bitor" | "bitxor" | "shl" | "shr") {
        
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
    
    e.op = word;
    inst.operator = true;
    inst.operand_kind = k;
}

fn untyped_int_literal(e: &Expr) -> Option<&str> {
    if !e.is_leaf() || e.quote != 0 {
        return None;
    }
    match e.lit {
        LitKind::LitInt => Some(e.val.as_str()),
        LitKind::LitNone if matches!(literal_text_kind(&e.val), Some("int64")) => {
            Some(e.val.as_str())
        }
        _ => None,
    }
}

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
        (Some(_), Some(_)) => return None, 
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

pub type StructTable = HashMap<String, ast::StructDecl>;

fn struct_lookup<'a>(ty: &str, structs: &'a StructTable) -> Option<&'a ast::StructDecl> {
    let n = ty.strip_prefix("/lib/").unwrap_or(ty);
    structs.get(n)
}

fn struct_field_type(ty: &str, part: &str, structs: &StructTable) -> Option<String> {
    let declaration = struct_lookup(ty, structs)?;
    let field = declaration.fields.iter().find(|f| f.name == part)?;
    Some(field.ty.clone())
}

pub(crate) fn slot_is_plain_name(w: &str) -> bool {
    !w.is_empty()
        && !w.starts_with('/')
        && !w.starts_with('*')
        && !w.starts_with("(*")
        
        && !w.contains(keytree::MEMBER_SEP)
        && !w.contains('[')
        && !w.contains(']')
}

fn is_none_lit(e: &Expr) -> bool {
    e.is_leaf() && e.val == "None"
}

pub(crate) fn container_elem(ty: &str) -> Option<String> {
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

pub fn container_value(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    if !super::kvkind::is_map_langtype(t) {
        return None;
    }
    
    let rest = &t[1..];
    let closing = rest.find('\'')?;
    let value = &rest[closing + 1..];
    if value.is_empty() {
        return None;
    }
    Some(value.to_string())
}

pub fn call_result_type(
    operation: &str,
    argument_types: &[Option<String>],
    argument_literals: &[Option<String>],
) -> Option<String> {
    match operation {
        
        "string·concat" => {
            
            let types: Vec<String> = argument_types
                .iter()
                .enumerate()
                .map(|(index, t)| match t {
                    Some(known) if !known.is_empty() => known.clone(),
                    _ => match argument_literals.get(index).and_then(|l| l.as_deref()) {
                        Some(_) => super::kvkind::KIND_CHAR.to_string(),
                        None => String::new(),
                    },
                })
                .collect();
            concat_result_encoding(&types)
        }
        
        "kvspace·listat" => container_key_type(argument_types.first()?.as_deref()?),
        
        "scatterarray·newscatter" => compact_to_scatter(argument_types.first()?.as_deref()?),
        "scatterarray·newcompact" => scatter_to_compact(argument_types.first()?.as_deref()?),
        
        "compactarray·newslice" => compact_argument_type(argument_types.first()?.as_deref()?),
        
        "compactarray·at" => container_elem(argument_types.first()?.as_deref()?),
        
        "compactarray·newreinterpret" => reinterpret_target_type(
            argument_literals
                .get(1)
                .and_then(|literal| literal.as_deref()),
        ),
        _ => None,
    }
}

fn compact_to_scatter(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    
    if super::kvkind::is_map_langtype(t) || !t.starts_with('[') {
        return None;
    }
    let element = container_elem(t)?;
    
    Some(format!("'[int64]'{element}"))
}

fn scatter_to_compact(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    if !super::kvkind::is_map_langtype(t) {
        return None;
    }
    let value = container_value(t)?;
    if value.is_empty() {
        return None;
    }
    Some(format!("[]{value}"))
}

fn compact_argument_type(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    if super::kvkind::is_map_langtype(t) || !t.starts_with('[') {
        return None;
    }
    Some(t.to_string())
}

fn reinterpret_target_type(literal: Option<&str>) -> Option<String> {
    let text = literal?.trim();
    if text.is_empty() || crate::langtype::has_union(text) || !crate::langtype::valid_langtype(text)
    {
        return None;
    }
    Some(text.to_string())
}

pub fn container_key_type(ty: &str) -> Option<String> {
    let t = ty.trim_start_matches('*');
    
    if !super::kvkind::is_map_langtype(t) {
        return None;
    }
    let closing = t[1..].find('\'')? + 1;
    let key = &t[1..closing];
    if key.starts_with("/lib/") {
        return None;
    }
    let Some(inner) = key
        .strip_prefix('[')
        .and_then(|rest| rest.strip_suffix(']'))
    else {
        
        return if key.is_empty() {
            None
        } else {
            Some(key.to_string())
        };
    };
    
    if inner.starts_with(']') {
        return Some(key.to_string());
    }
    if inner.contains(',') || inner.contains(']') {
        return None;
    }
    Some(inner.to_string())
}

pub fn resolve_slot_type(
    w: &str,
    _tm: &HashMap<String, String>,
    structs: &StructTable,
    declared: &HashMap<String, String>,
) -> Option<String> {
    if slot_is_plain_name(w) {
        return declared.get(w).cloned();
    }
    
    if let Some(rest) = w.strip_prefix("(*") {
        if let Some((base, tail)) = rest.split_once(keytree::MEMBER_SEP) {
            let inner = base.strip_suffix(')')?;
            let t = declared.get(inner)?;
            return Some(member_chain_type(deref_value_type(t), tail, structs)?);
        }
        
        let inner = rest.strip_suffix(')')?;
        let t = declared.get(inner)?;
        return Some(deref_value_type(t).to_string());
    }
    if let Some(rest) = w.strip_prefix('*') {
        let t = declared.get(rest)?;
        return Some(deref_value_type(t).to_string());
    }
    
    if !w.contains(keytree::MEMBER_SEP) {
        if let Some(open) = w.find('[') {
            let base = &w[..open];
            let t = declared.get(base)?;
            return container_elem(t);
        }
    }
    let mut it = w.split(keytree::MEMBER_SEP);
    let base = it.next()?;
    let ty = declared.get(base)?.clone();
    let tail: Vec<&str> = it.collect();
    
    if !tail.is_empty()
        && tail
            .iter()
            .all(|segment| segment.starts_with('[') && segment.ends_with(']'))
    {
        return container_value(&ty);
    }
    member_chain_type(&ty, &tail.join(&keytree::MEMBER_SEP.to_string()), structs)
}

pub(crate) fn base_is_path_string(base: &str) -> bool {
    let bare = base.trim_start_matches('*');
    !super::kvkind::is_map_langtype(bare)
        && super::kvkind::is_char_kind(super::kvkind::parse_langtype(bare).1.as_str())
}

pub(crate) fn member_chain_type(start: &str, tail: &str, structs: &StructTable) -> Option<String> {
    let mut ty = start.to_string();
    if tail.is_empty() {
        return Some(ty);
    }
    for (index, part) in tail.split(keytree::MEMBER_SEP).enumerate() {
        
        if index > 0 {
            if let Some(inner) = ty.strip_prefix('*') {
                ty = inner.to_string();
            }
        }
        
        if base_is_path_string(&ty) {
            return None;
        }
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

pub fn check_field_default(name: &str, ty: &str, e: &Expr) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    if ty.starts_with('*') || ty.starts_with('@') {
        return diags; 
    }
    let (dims, base) = super::kvkind::parse_langtype(ty);
    let scalar = dims.is_empty()
        && (builtin::is_int_kind(&base) || builtin::is_float_kind(&base) || base == "bool");
    if !scalar {
        return diags; 
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
                format!(
                    "struct field {name:?}: bool default must be true/false, got {:?}",
                    e.val
                ),
            );
        }
    } else if !check_literal(&base, e, &mut diags) {
        push_write_diag(
            &mut diags,
            format!(
                "struct field {name:?}: default for {ty} must be a numeric literal, got {:?}",
                e.val
            ),
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
        
        if rhs != target {
            push_write_diag(diags, format!("TypeError: cannot assign {rhs} to {target}"));
        }
    }
}

fn check_rhs(target: &str, e: &Expr, rhs: &str, nullable: bool, diags: &mut Vec<Diagnostic>) {
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

fn expr_result_type(
    e: &Expr,
    tm: &HashMap<String, String>,
    annotated: &str,
    structs: &StructTable,
    output_type_failure: &mut Option<String>,
) -> String {
    if e.is_leaf() {
        return arg_type(e, tm);
    }
    
    if e.op == ast::DEREF_OP {
        let inner = e.args.first().map(|a| arg_type(a, tm)).unwrap_or_default();
        return deref_value_type(&inner).to_string();
    }
    
    if e.op == ast::MEMBER_READ_OP {
        let base = e.args.first().map(|a| arg_type(a, tm)).unwrap_or_default();
        if base.is_empty() {
            return String::new();
        }
        let tail: Vec<&str> = e.args.iter().skip(1).map(|a| a.val.as_str()).collect();
        return member_chain_type(
            &base,
            &tail.join(&keytree::MEMBER_SEP.to_string()),
            &StructTable::default(),
        )
        .unwrap_or_default();
    }
    if matches!(e.op.as_str(), "compactarray·new" | "map·new") {
        return annotated.to_string();
    }
    let sym = symbol::lookup(&e.op);
    if sym.arith {
        
        let known: Vec<String> = e
            .args
            .iter()
            .map(|a| arg_type(a, tm))
            .filter_map(|t| concat_operand_type(&t))
            .collect();
        if known.iter().any(|t| is_string_kind(t)) {
            
            if known.len() < e.args.len() {
                *output_type_failure = Some(format!(
                    "concat 的结果类型无法确定：{} 个操作数里只有 {} 个有静态类型",
                    e.args.len(),
                    known.len()
                ));
                return String::new();
            }
            return match concat_result_encoding(&known) {
                Some(t) => t,
                None => {
                    *output_type_failure = Some(format!(
                        "concat 的结果类型无法确定：操作数编码 {} 没有共同编码",
                        known
                            .iter()
                            .map(|t| format!("`{t}`"))
                            .collect::<Vec<_>>()
                            .join(" 与 ")
                    ));
                    String::new()
                }
            };
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
    if e.op == "kvspace/key·abs" {
        let t = e.args.first().map(|a| arg_type(a, tm)).unwrap_or_default();
        return format!("*{t}");
    }
    
    if e.op == "kvlang·resolvekey" {
        
        let base = e.args.first().map(|a| arg_type(a, tm)).unwrap_or_default();
        let container = deref_value_type(&base);
        
        if base_is_path_string(container) {
            return String::new();
        }
        let member = container_value(container)
            .or_else(|| container_elem(container))
            .unwrap_or_default();
        if member.is_empty() {
            return String::new();
        }
        return format!("*{member}");
    }
    if is_cast_op(&e.op) {
        return e.op.clone();
    }
    let mut tm2 = tm.clone();
    let reads: Vec<String> = e.args.iter().map(|a| a.val.clone()).collect();
    infer_op_type(
        &e.op,
        &reads,
        &mut tm2,
        annotated,
        structs,
        output_type_failure,
    )
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
    annotated: &std::collections::HashSet<String>,
    diags: &mut Vec<Diagnostic>,
) {
    for st in body {
        match st {
            Stmt::Instruction(s) => walk_inst(s, sigs, tm, structs, declared, annotated, diags),
            Stmt::Scope(s) => walk_writes(&s.body, sigs, tm, structs, declared, annotated, diags),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    walk_inst(c, sigs, tm, structs, declared, annotated, diags);
                }
                walk_writes(&s.then_, sigs, tm, structs, declared, annotated, diags);
                walk_writes(&s.else_, sigs, tm, structs, declared, annotated, diags);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    walk_inst(c, sigs, tm, structs, declared, annotated, diags);
                }
                walk_writes(&s.body, sigs, tm, structs, declared, annotated, diags);
            }
            Stmt::For(s) => walk_writes(&s.body, sigs, tm, structs, declared, annotated, diags),
            _ => {}
        }
    }
}

pub type SigTable = HashMap<String, ast::FuncSig>;

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

fn check_call_args(
    e: &Expr,
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    declared: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    let sym = symbol::lookup(&e.op);
    if sym.arith
        || sym.cmp
        || is_cast_op(&e.op)
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
        
        let raw = param
            .pointer_type()
            .trim_end_matches("...")
            .trim()
            .to_string();
        if raw.is_empty() || raw.starts_with('@') {
            continue;
        }
        
        let declared_value_type = deref_value_type(&raw).to_string();
        
        if is_matchable_kindexpr(&declared_value_type) {
            if let Some(vt) = provable_arg_type(arg, declared, tm) {
                
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

fn is_variable_arg(e: &Expr) -> bool {
    e.is_leaf() && e.quote == 0 && !e.val.is_empty() && literal_slot_type(&e.val).is_empty()
}

fn is_matchable_kindexpr(ty: &str) -> bool {
    !ty.is_empty()
        && !ty.starts_with("/lib/")
        && !ty.contains(keytree::MEMBER_SEP)
        && ty != "stringkeymap"
}

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

fn provable_arg_type(
    e: &Expr,
    declared: &HashMap<String, String>,
    tm: &HashMap<String, String>,
) -> Option<String> {
    
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

fn is_concat_instruction(s: &Instruction, tm: &HashMap<String, String>) -> bool {
    let Some(e) = s.expr.as_ref() else {
        return false;
    };
    if e.op != "string·concat" && !matches!(e.op.as_str(), "+" | "add") {
        return false;
    }
    
    if e.op == "string·concat" {
        return true;
    }
    e.args.iter().any(|a| is_string_kind(&arg_type(a, tm)))
}

fn concat_conflict_message(e: &Expr, tm: &HashMap<String, String>) -> String {
    let operand_types: Vec<String> = e
        .args
        .iter()
        .map(|a| {
            let t = slot_type(&a.val, tm);
            if t.is_empty() {
                lit_to_type(a.lit).to_string()
            } else {
                t
            }
        })
        .collect();
    format!(
        "concat 的结果类型无法确定：操作数编码 {} 没有共同编码",
        operand_types
            .iter()
            .filter(|t| !t.is_empty())
            .map(|t| format!("`{t}`"))
            .collect::<Vec<_>>()
            .join(" 与 ")
    )
}

fn is_layout_temporary_slot(name: &str) -> bool {
    let Some(rest) = name.strip_prefix('_') else {
        return false;
    };
    !rest.is_empty()
        && rest
            .chars()
            .all(|c| c.is_ascii_digit() || c.is_ascii_lowercase() || c == '_')
}

fn check_declared_write_slots(
    s: &Instruction,
    tm: &HashMap<String, String>,
    annotated: &std::collections::HashSet<String>,
    diags: &mut Vec<Diagnostic>,
) {
    
    if s.pos.line == 0 {
        return;
    }
    for w in &s.writes {
        if !slot_is_plain_name(w) || annotated.contains(w) {
            continue;
        }
        
        if w == "_" {
            continue;
        }
        
        if is_layout_temporary_slot(w) {
            continue;
        }
        let inferred = tm.get(w).map(String::as_str).unwrap_or("");
        let hint = if inferred.is_empty() {
            String::new()
        } else {
            format!("（当前推断为 {inferred}）")
        };
        diags.push(Diagnostic {
            pos: s.pos,
            warn: false,
            info: false,
            message: format!("变量 '{w}' 未声明类型；请写成 '{w}:T'{hint}"),
            source: String::new(),
            src_file: String::new(),
            src_name: String::new(),
        });
    }
}

fn walk_inst(
    s: &Instruction,
    sigs: &SigTable,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    declared: &HashMap<String, String>,
    annotated: &std::collections::HashSet<String>,
    diags: &mut Vec<Diagnostic>,
) {
    let Some(e) = &s.expr else { return };
    check_declared_write_slots(s, tm, annotated, diags);
    check_call_args(e, sigs, tm, declared, diags);
    check_implicit_deref_member(e, tm, declared, diags);
    check_resolve_write_slot(s, e, tm, diags);
    if e.op == format!("struct{}new", keytree::MEMBER_SEP) {
        check_struct_literal(e, structs, diags);
    }
    
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
    let mut type_failure = None;
    let rhs = expr_result_type(e, tm, annotated, structs, &mut type_failure);
    
    if type_failure.is_none() && rhs.is_empty() && is_concat_instruction(s, tm) {
        
        let known: Vec<String> = e
            .args
            .iter()
            .map(|a| arg_type(a, tm))
            .filter(|t| !t.is_empty())
            .collect();
        if known.len() < e.args.len() {
            type_failure = Some(format!(
                "concat 的结果类型无法确定：{} 个操作数里只有 {} 个有静态类型——推不出类型的操作数不参与编码收敛",
                e.args.len(),
                known.len()
            ));
        } else if !known.is_empty() && concat_result_encoding(&known).is_none() {
            type_failure = Some(concat_conflict_message(e, tm));
        }
    }
    if let Some(message) = type_failure {
        let slot = s.writes.first().cloned().unwrap_or_default();
        push_write_diag(diags, format!("{message}（槽 `{slot}`）"));
    }
    for (j, w) in s.writes.iter().enumerate() {
        if w.is_empty() {
            return;
        }
        let ann = s.write_types.get(j).map(String::as_str).unwrap_or("");
        let ty = if !ann.is_empty() {
            ann.to_string()
        } else {
            
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
            
            let nullable = declared
                .get(w.as_str())
                .or_else(|| tm.get(w.as_str()))
                .is_some_and(|t| t.starts_with('*'));
            check_rhs(&ty, e, &rhs, nullable, diags);
        }
    }
}

pub fn check_call_shapes(fn_: &Func, sigs: &SigTable) -> Vec<Diagnostic> {
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
    
    for arg in &e.args {
        check_one_call_shape(arg, sigs, tm, diags);
    }
}

pub fn check_inplace_unit(
    facts: &[InplaceFacts],
    resolve: &dyn Fn(&str) -> InplaceCallee,
) -> Vec<Vec<Diagnostic>> {
    let resolved: Vec<Vec<InplaceCallee>> = facts
        .iter()
        .map(|f| f.calls.iter().map(|(callee, _)| resolve(callee)).collect())
        .collect();
    
    let judge = |fi: usize, name: &str, credited: &[Vec<bool>]| -> (bool, bool, bool) {
        let f = &facts[fi];
        let delegated = f
            .calls
            .iter()
            .zip(&resolved[fi])
            .any(|((_, slots), callee)| {
                slots.iter().enumerate().any(|(j, slot)| {
                    slot == name
                        && match callee {
                            InplaceCallee::Unit(g) => credited[*g].get(j).copied().unwrap_or(false),
                            InplaceCallee::Marks(marks) => marks.get(j).copied().unwrap_or(false),
                            InplaceCallee::Unknown => false,
                        }
                })
            });
        (
            f.reads.contains(name) || delegated,
            f.writes.contains(name),
            f.same.contains(name) || delegated,
        )
    };
    let mut credited: Vec<Vec<bool>> = facts.iter().map(|f| vec![false; f.nwrites]).collect();
    loop {
        let mut changed = false;
        for (fi, f) in facts.iter().enumerate() {
            for (name, k) in &f.marks {
                if !credited[fi][*k] && judge(fi, name, &credited) == (true, true, true) {
                    credited[fi][*k] = true;
                    changed = true;
                }
            }
        }
        if !changed {
            break;
        }
    }
    facts
        .iter()
        .enumerate()
        .map(|(fi, f)| {
            let mut diags = Vec::new();
            for (name, k) in &f.marks {
                if credited[fi][*k] {
                    continue;
                }
                let (read, written, _) = judge(fi, name, &credited);
                let message = if !read {
                    format!("inplace 写参 `{name}` 在函数体内从未被读；`inplace` 要求「同一指令内既读又写」")
                } else if !written {
                    format!("inplace 写参 `{name}` 在函数体内从未被写；`inplace` 要求「同一指令内既读又写」")
                } else {
                    format!(
                        "inplace 写参 `{name}` 只是「先读后写」或「先写后读」，没有**同一条指令**同时读写它——那不是 inplace，请去掉修饰符"
                    )
                };
                push_write_diag(&mut diags, message);
            }
            diags
        })
        .collect()
}

#[derive(Clone)]
pub enum InplaceCallee {
    
    Unit(usize),
    
    Marks(Vec<bool>),
    Unknown,
}

pub struct InplaceFacts {
    marks: Vec<(String, usize)>,
    nwrites: usize,
    reads: HashSet<String>,
    writes: HashSet<String>,
    same: HashSet<String>,
    
    calls: Vec<(String, Vec<String>)>,
}

impl InplaceFacts {
    pub fn callees(&self) -> impl Iterator<Item = &str> {
        self.calls.iter().map(|(callee, _)| callee.as_str())
    }
}

pub fn inplace_facts(fn_: &Func) -> InplaceFacts {
    let mut coords: HashMap<String, String> = HashMap::new();
    for (k, p) in fn_.sig.params.iter().enumerate() {
        coords.insert(format!("[0,-{}]", k + 1), p.name.clone());
    }
    for (k, p) in fn_.sig.returns.iter().enumerate() {
        coords.insert(format!("[0,{}]", k + 1), p.name.clone());
    }
    let mut facts = InplaceFacts {
        marks: fn_
            .sig
            .returns
            .iter()
            .enumerate()
            .filter(|(_, p)| p.inplace)
            .map(|(k, p)| (p.name.clone(), k))
            .collect(),
        nwrites: fn_.sig.returns.len(),
        reads: HashSet::new(),
        writes: HashSet::new(),
        same: HashSet::new(),
        calls: Vec::new(),
    };
    let mut positions: HashMap<String, String> = HashMap::new();
    walk_inplace_usage(&fn_.body, &coords, &mut positions, &mut facts);
    facts
}

fn usage_name(
    text: &str,
    coords: &HashMap<String, String>,
    positions: &HashMap<String, String>,
) -> String {
    let base = inplace_slot_base(text);
    let bare = base.trim_start_matches('*');
    if let Some(name) = coords.get(bare) {
        return name.clone();
    }
    if text.trim_start().trim_start_matches('(').starts_with('*') {
        if let Some(name) = positions.get(bare) {
            return name.clone();
        }
    }
    bare.to_string()
}

fn inplace_slot_base(text: &str) -> String {
    let base = text
        .split(keytree::MEMBER_SEP)
        .next()
        .unwrap_or(text)
        .trim();
    let trimmed_star = base.trim_start_matches('*').trim();
    let base = if !trimmed_star.starts_with('[') {
        base.split('[').next().unwrap_or(base).trim()
    } else {
        base
    };
    normalize_slot_name(base).0
}

fn collect_operand_names(e: &Expr, out: &mut Vec<String>) {
    if e.is_leaf() {
        if e.quote == 0 && !e.val.is_empty() {
            out.push(e.val.clone());
        }
        return;
    }
    for (i, arg) in e.args.iter().enumerate() {
        if e.op == "goto" || (e.op == "br" && i > 0) {
            continue;
        }
        collect_operand_names(arg, out);
    }
}

fn record_inst_inplace_usage(
    inst: &Instruction,
    coords: &HashMap<String, String>,
    positions: &mut HashMap<String, String>,
    facts: &mut InplaceFacts,
) {
    
    let mut raw = Vec::new();
    if let Some(e) = &inst.expr {
        collect_operand_names(e, &mut raw);
    }
    let reads: Vec<String> = raw
        .iter()
        .map(|r| usage_name(r, coords, positions))
        .collect();
    let writes: Vec<String> = inst
        .writes
        .iter()
        .map(|w| usage_name(w, coords, positions))
        .collect();
    if let Some(e) = &inst.expr {
        
        if e.op == "kvlang·resolvekey" && inst.writes.len() == 1 {
            let mut root = e;
            while root.op == "kvlang·resolvekey" && !root.args.is_empty() {
                root = &root.args[0];
            }
            
            let mut names = Vec::new();
            collect_operand_names(root, &mut names);
            if let [only] = names.as_slice() {
                let base = usage_name(only, coords, positions);
                positions.insert(inst.writes[0].clone(), base);
            }
        }
        if !e.is_leaf() {
            facts.calls.push((e.op.clone(), writes.clone()));
        }
    }
    
    for r in reads.iter().filter(|r| !r.is_empty()) {
        facts.reads.insert(r.clone());
        if writes.contains(r) {
            facts.same.insert(r.clone());
        }
    }
    for w in writes.iter().filter(|w| !w.is_empty()) {
        facts.writes.insert(w.clone());
    }
}

fn walk_inplace_usage(
    body: &[Stmt],
    coords: &HashMap<String, String>,
    positions: &mut HashMap<String, String>,
    facts: &mut InplaceFacts,
) {
    for st in body {
        match st {
            Stmt::Instruction(inst) => record_inst_inplace_usage(inst, coords, positions, facts),
            Stmt::Scope(s) => walk_inplace_usage(&s.body, coords, positions, facts),
            Stmt::If(s) => {
                if let Some(c) = &s.cond {
                    record_inst_inplace_usage(c, coords, positions, facts);
                }
                walk_inplace_usage(&s.then_, coords, positions, facts);
                walk_inplace_usage(&s.else_, coords, positions, facts);
            }
            Stmt::While(s) => {
                if let Some(c) = &s.cond {
                    record_inst_inplace_usage(c, coords, positions, facts);
                }
                walk_inplace_usage(&s.body, coords, positions, facts);
            }
            Stmt::For(s) => {
                let mut raw = Vec::new();
                collect_operand_names(&s.iter, &mut raw);
                for r in raw {
                    facts.reads.insert(usage_name(&r, coords, positions));
                }
                walk_inplace_usage(&s.body, coords, positions, facts);
            }
            _ => {}
        }
    }
}

pub fn check_for_shadowing(fn_: &Func, diags: &mut Vec<Diagnostic>) {
    fn walk(body: &[Stmt], names: &HashSet<&str>, diags: &mut Vec<Diagnostic>) {
        for st in body {
            match st {
                Stmt::For(s) => {
                    if names.contains(s.var.as_str()) {
                        push_write_diag(
                            diags,
                            format!(
                                "for-loop variable `{}` shadows a parameter of the same name; rename it",
                                s.var
                            ),
                        );
                    }
                    walk(&s.body, names, diags);
                }
                Stmt::Scope(s) => walk(&s.body, names, diags),
                Stmt::If(s) => {
                    walk(&s.then_, names, diags);
                    walk(&s.else_, names, diags);
                }
                Stmt::While(s) => walk(&s.body, names, diags),
                _ => {}
            }
        }
    }
    let names: HashSet<&str> = fn_
        .sig
        .params
        .iter()
        .chain(&fn_.sig.returns)
        .map(|p| p.name.as_str())
        .collect();
    walk(&fn_.body, &names, diags);
}

pub fn validate_writes(
    fn_: &Func,
    tm: &HashMap<String, String>,
    structs: &StructTable,
    sigs: &SigTable,
    annotated: &std::collections::HashSet<String>,
) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    
    let declared = declared_langtypes_by_origin(fn_);
    walk_writes(
        &fn_.body, sigs, tm, structs, &declared, annotated, &mut diags,
    );
    diags
}

fn check_resolve_write_slot(
    s: &Instruction,
    e: &Expr,
    tm: &HashMap<String, String>,
    diags: &mut Vec<Diagnostic>,
) {
    if e.op != "kvlang·resolvekey" || s.writes.len() != 1 {
        return;
    }
    
    let write_name = s.writes.first().cloned().unwrap_or_default();
    let is_internal_position_slot =
        write_name.starts_with("_p") && write_name[2..].chars().all(|c| c.is_ascii_digit());
    if !is_internal_position_slot {
        return;
    }
    
    if s.write_types.first().is_some_and(|t| !t.is_empty()) {
        return;
    }
    
    let write_name = s.writes.first().cloned().unwrap_or_default();
    let display = if write_name.starts_with('_') {
        let base_text = e.args.first().map(|a| a.val.as_str()).unwrap_or("");
        let key_text = e.args.get(1).map(|a| a.val.as_str()).unwrap_or("");
        format!("{base_text}·*{key_text}")
    } else {
        write_name.clone()
    };
    let base_argument = e.args.first();
    let base = base_argument.map(|a| a.val.as_str()).unwrap_or("");
    
    let base_type = match base_argument {
        Some(a) if a.lit != LitKind::LitNone => lit_to_type(a.lit).to_string(),
        _ => slot_type(base, tm),
    };
    
    let container = deref_value_type(&base_type);
    
    if container_value(container).is_some() {
        return;
    }
    
    if container_elem(container).is_some()
        && !(!container.contains(super::keytree::MEMBER_SEP) && is_string_kind(container))
    {
        return;
    }
    
    if base_type.starts_with('*') {
        return;
    }
    
    if base.starts_with('_') && base.chars().all(|c| c == '_' || c.is_alphanumeric()) {
        return;
    }
    
    let is_path_text =
        !super::kvkind::is_map_langtype(deref_value_type(&base_type)) && is_string_kind(&base_type);
    let base_kind = if base_type.is_empty() {
        "未知"
    } else if is_path_text {
        "字符串（父路径文本）"
    } else {
        "非容器"
    };
    diags.push(Diagnostic {
        pos: Pos { line: 0, col: 0 },
        warn: false,
        info: false,
        message: format!(
            "kvspace/key·deref（由 `{display}` 展开）：写槽 {:?} 需要显式类型注解\
             （如 `{}:T = …`）——base {base:?} 的形态是{base_kind}，成员类型无法静态得出",
            display, write_name
        ),
        source: String::new(),
        src_file: String::new(),
        src_name: String::new(),
    });
}

#[cfg(test)]
mod union_match_tests {
    use super::{normalize_slot_name, value_matches};

    #[test]
    fn slot_name_normalization_covers_both_deref_spellings() {
        
        assert_eq!(normalize_slot_name("p"), ("p".to_string(), false));
        assert_eq!(normalize_slot_name("*p"), ("p".to_string(), true));
        assert_eq!(normalize_slot_name("(*p)"), ("p".to_string(), true));
        assert_eq!(normalize_slot_name("((*p))"), ("p".to_string(), true));
        assert_eq!(normalize_slot_name("( *p )"), ("p".to_string(), true));
        assert_eq!(normalize_slot_name("*_8"), ("_8".to_string(), true));
        
        assert_eq!(
            normalize_slot_name("*(a + 1)"),
            ("*(a + 1)".to_string(), false)
        );
        assert_eq!(normalize_slot_name("a·b"), ("a·b".to_string(), false));
        assert_eq!(normalize_slot_name("/lib/x"), ("/lib/x".to_string(), false));
        assert_eq!(
            normalize_slot_name("(a) + (b)"),
            (("(a) + (b)").to_string(), false)
        );
        assert_eq!(normalize_slot_name(""), ("".to_string(), false));
    }

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
    fn value_form_is_what_assignment_checks_need() {
        
        assert!(!value_matches("*int64", "int64"));
        
        assert!(value_matches("int64", "int64"));
        
        assert!(value_matches("*TNode", "*TNode"));
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
    
    if !base.is_leaf() || base.quote != 0 || base.val.is_empty() {
        return;
    }
    
    if base.val.starts_with('*') {
        return;
    }
    
    if base.val.starts_with('_') && base.val[1..].chars().all(|c| c.is_ascii_digit()) {
        return;
    }
    let ty = declared
        .get(&base.val)
        .or_else(|| tm.get(&base.val))
        .filter(|t| t.starts_with('*'));
    let Some(ty) = ty else { return };
    
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
        
        assert_eq!(
            call_result_type(
                "kvspace·listat",
                &types(&["\'[]char/utf32\'int64"]),
                &no_literals()
            ),
            Some("[]char/utf32".to_string())
        );
        assert_eq!(
            call_result_type(
                "kvspace·listat",
                &types(&["\'[int64]\'int64"]),
                &no_literals()
            ),
            Some("int64".to_string())
        );
        assert_eq!(
            call_result_type(
                "kvspace·listat",
                &types(&["\'int32\'int32"]),
                &no_literals()
            ),
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
            call_result_type(
                "kvspace·listat",
                &types(&["\'[int64,int64]\'int64"]),
                &no_literals()
            ),
            None
        );
        assert_eq!(
            call_result_type("kvspace·listat", &[None], &no_literals()),
            None
        );
        assert_eq!(
            call_result_type("kvspace·listat", &[], &no_literals()),
            None
        );
    }

    #[test]
    fn form_transforms_are_symmetric_and_fail_closed() {
        
        assert_eq!(
            call_result_type(
                "scatterarray·newscatter",
                &types(&["[]int64"]),
                &no_literals()
            ),
            Some("\'[int64]\'int64".to_string())
        );
        assert_eq!(
            call_result_type(
                "scatterarray·newcompact",
                &types(&["\'[int64]\'int64"]),
                &no_literals()
            ),
            Some("[]int64".to_string())
        );
        
        assert_eq!(
            call_result_type(
                "scatterarray·newscatter",
                &types(&["\'[int64]\'int64"]),
                &no_literals()
            ),
            None
        );
        assert_eq!(
            call_result_type(
                "scatterarray·newcompact",
                &types(&["[]int64"]),
                &no_literals()
            ),
            None
        );
        
        assert_eq!(
            call_result_type(
                "compactarray·newslice",
                &types(&["[]int64"]),
                &no_literals()
            ),
            Some("[]int64".to_string())
        );
        assert_eq!(
            call_result_type(
                "compactarray·newslice",
                &types(&["\'[int64]\'int64"]),
                &no_literals()
            ),
            None
        );
        
        assert_eq!(
            call_result_type("compactarray·at", &types(&["[]int64"]), &no_literals()),
            Some("int64".to_string())
        );
    }

    #[test]
    fn reinterpret_reads_the_literal_but_still_validates_it() {
        let with = |text: &str| vec![None, Some(text.to_string())];
        assert_eq!(
            call_result_type(
                "compactarray·newreinterpret",
                &types(&["[]int64"]),
                &with("[]uint8")
            ),
            Some("[]uint8".to_string())
        );
        
        assert_eq!(
            call_result_type(
                "compactarray·newreinterpret",
                &types(&["[]int64"]),
                &with("int64|float64")
            ),
            None
        );
        assert_eq!(
            call_result_type(
                "compactarray·newreinterpret",
                &types(&["[]int64"]),
                &with("garbage")
            ),
            None
        );
        assert_eq!(
            call_result_type(
                "compactarray·newreinterpret",
                &types(&["[]int64"]),
                &with("")
            ),
            None
        );
        assert_eq!(
            call_result_type(
                "compactarray·newreinterpret",
                &types(&["[]int64"]),
                &no_literals()
            ),
            None
        );
    }

    #[test]
    fn operators_outside_the_rule_table_get_no_type() {
        
        assert_eq!(
            call_result_type("string·concat", &types(&["char/utf32"]), &no_literals()),
            Some("[]char/utf32".to_string())
        );
        assert_eq!(
            call_result_type("compactarray·fill", &types(&["[]int64"]), &no_literals()),
            None
        );
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
        
        assert_eq!(
            resolve_slot_type("(*r)", &HashMap::new(), &structs, &declared),
            Some("[]char/utf32".to_string())
        );
        
        assert_eq!(
            resolve_slot_type("(x)", &HashMap::new(), &structs, &declared),
            None
        );
    }
}
