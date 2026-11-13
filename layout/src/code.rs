
use crate::scanner::Pos;
use std::collections::{HashMap, HashSet};

use super::ast::{
    self, Expr, Func, FuncSig, Instruction, Param, RwirDecl, ScopeStmt, Stmt, StructDecl,
};
use super::ffi::Kv;
use super::{builtin, crash_resume, ffi, keytree, kvkind, lower, parser};

pub fn init_dirs(kv: &mut Kv) -> Result<(), String> {
    kv.mkindex("/lib/")?;
    kv.mkindex("/vthread/")?;
    Ok(())
}

pub fn compile(kv: &mut Kv, src: &str) -> Result<Vec<String>, String> {
    
    let checked = analyze_file(src, &mut |name| store_callee(kv, name))?;
    let file = &checked.file;

    let mut inits: Vec<String> = Vec::new();
    let local_funcs = unit_func_names(file);
    
    for decl in &file.structs {
        write_struct_decl(kv, decl)?;
    }
    for decl in &file.rwir_decls {
        write_rwir_decl(kv, decl)?;
    }
    for a in &checked.funcs {
        write_func(kv, a, &local_funcs, &checked.structs)?;
        if a.is_init {
            inits.push(init_fn_name(&a.pkg));
        }
    }
    if let Some(a) = &checked.top_init {
        write_func(kv, a, &local_funcs, &checked.structs)?;
        inits.push(init_fn_name(""));
    }
    Ok(inits)
}

pub struct AnalyzedFunc {
    pkg: String,
    func: Func,
    declared: HashMap<String, String>,
    type_sets: HashMap<String, Vec<String>>,
    
    stable: HashMap<String, String>,
    seq: Vec<Instruction>,
    labels: HashMap<String, i32>,
    is_init: bool,
    
    crash_resume: Option<crash_resume::Resumability>,
    
    slot_types: Vec<Vec<String>>,
}

pub struct CheckedFile {
    file: ast::File,
    structs: lower::StructTable,
    funcs: Vec<AnalyzedFunc>,
    top_init: Option<AnalyzedFunc>,
}

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
        format!(
            "{stage}: error-level diagnostics — refusing to load ({first}; and {} more)",
            errors - 1
        )
    } else {
        format!("{stage}: error-level diagnostics — refusing to load ({first})")
    }
}

fn collect_struct_table(file: &ast::File) -> lower::StructTable {
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
    structs
}

pub enum StoreCallee {
    Rwfunc(Vec<bool>),
    DefRwir(Vec<bool>),
}

fn store_callee(kv: &mut Kv, op: &str) -> Option<StoreCallee> {
    let name = op.strip_prefix("/lib/").unwrap_or(op);
    if name.is_empty() {
        return None;
    }
    let dir = format!("/lib/{name}/");
    if kvkind::kind(&kv.get_one(&dir), &kv.langtype_of(&dir)) == kvkind::KIND_RWFUNC {
        let (nr, nw, dynamic) = kvkind::counts(&kv.get_one(&format!("{dir}[0,0]")));
        let sig = reconstruct_sig(kv, &dir, name, nr, nw, dynamic);
        return Some(StoreCallee::Rwfunc(
            sig.returns.iter().map(|p| p.inplace).collect(),
        ));
    }
    let decl = keytree::rwir(name);
    let value = kv.get_one(&decl);
    if kvkind::kind(&value, &kv.langtype_of(&decl)) == kvkind::KIND_DEF_RWIR {
        let (_, nw, _) = kvkind::counts(&value);
        let marks = (1..=nw)
            .map(|k| {
                kvkind::def_langtype_full(&kv.get_one(&format!("{decl}/[0,{k}]")))
                    .is_some_and(|(_, inplace)| inplace)
            })
            .collect();
        return Some(StoreCallee::DefRwir(marks));
    }
    None
}

struct UnitNames {
    funcs: HashMap<String, usize>,
    rwirs: HashMap<String, Vec<bool>>,
}

impl UnitNames {
    fn new(file: &ast::File) -> Self {
        let qualify = |pkg: &str, name: &str| {
            if pkg.is_empty() {
                name.to_string()
            } else {
                format!("{pkg}{}{name}", keytree::MEMBER_SEP)
            }
        };
        
        let mut funcs = HashMap::new();
        for (i, f) in file.funcs.iter().enumerate() {
            let pkg = if f.pkg.is_empty() {
                &file.package
            } else {
                &f.pkg
            };
            funcs.insert(qualify(pkg, &f.sig.name), i);
            funcs.insert(qualify(&f.pkg, &f.sig.name), i);
        }
        let mut rwirs = HashMap::new();
        for d in &file.rwir_decls {
            rwirs.insert(
                qualify(&d.pkg, &d.sig.name),
                d.sig.returns.iter().map(|p| p.inplace).collect(),
            );
        }
        UnitNames { funcs, rwirs }
    }
}

struct Externals<'a> {
    lookup: &'a mut dyn FnMut(&str) -> Option<StoreCallee>,
    cache: HashMap<String, Option<StoreCallee>>,
}

impl Externals<'_> {
    fn get(&mut self, name: &str) -> Option<&StoreCallee> {
        if !self.cache.contains_key(name) {
            let found = (self.lookup)(name);
            self.cache.insert(name.to_string(), found);
        }
        self.cache.get(name).and_then(Option::as_ref)
    }
}

fn inplace_callee(name: &str, unit: &UnitNames, ext: &mut Externals) -> lower::InplaceCallee {
    let name = name.strip_prefix("/lib/").unwrap_or(name);
    if let Some(&i) = unit.funcs.get(name) {
        return lower::InplaceCallee::Unit(i);
    }
    
    if is_operator_word(name) {
        return lower::InplaceCallee::Unknown;
    }
    if let Some(marks) = unit.rwirs.get(name) {
        return lower::InplaceCallee::Marks(marks.clone());
    }
    match ext.get(name) {
        Some(StoreCallee::Rwfunc(marks) | StoreCallee::DefRwir(marks)) => {
            return lower::InplaceCallee::Marks(marks.clone())
        }
        None => {}
    }
    match lower::native_rwir_sig(name) {
        Some(sig) => lower::InplaceCallee::Marks(sig.returns.iter().map(|p| p.inplace).collect()),
        None => lower::InplaceCallee::Unknown,
    }
}

fn step_callee(
    inst: &Instruction,
    op: &str,
    unit: &UnitNames,
    ext: &mut Externals,
) -> crash_resume::Callee {
    use crash_resume::Callee;
    if matches!(op, "" | "goto" | "br" | "return") {
        return Callee::None;
    }
    let verified = |name: &str| match crash_resume::op_spec(name) {
        Some(spec) => Callee::Op(spec),
        None => Callee::Unverified(name.to_string()),
    };
    if inst.operator || op == "=" {
        return verified(op);
    }
    let name = op.strip_prefix("/lib/").unwrap_or(op);
    if let Some(&i) = unit.funcs.get(name) {
        return Callee::Unit(i);
    }
    if let Some(StoreCallee::Rwfunc(_)) = ext.get(name) {
        return Callee::Store(name.to_string());
    }
    if let Some(spec) = crash_resume::op_spec(name) {
        return Callee::Op(spec);
    }
    let declared = unit.rwirs.contains_key(name)
        || matches!(ext.get(name), Some(StoreCallee::DefRwir(_)))
        || lower::native_rwir_sig(name).is_some()
        || lower::native_catalog_has(name);
    if declared {
        return Callee::Unverified(name.to_string());
    }
    Callee::Unknown(name.to_string())
}

fn local_slot_types(a: &AnalyzedFunc) -> HashMap<String, String> {
    let mut types: HashMap<String, String> = HashMap::new();
    for (inst, slot_types) in a.seq.iter().zip(&a.slot_types) {
        for (w, ty) in inst.writes.iter().zip(slot_types) {
            if ty.is_empty() || !w.chars().all(|c| c == '_' || c.is_alphanumeric()) {
                continue;
            }
            match types.get(w) {
                Some(seen) if seen != ty => {
                    types.insert(w.clone(), String::new());
                }
                Some(_) => {}
                None => {
                    types.insert(w.clone(), ty.clone());
                }
            }
        }
    }
    types
}

fn crash_resume_input(
    a: &AnalyzedFunc,
    unit: &UnitNames,
    ext: &mut Externals,
) -> crash_resume::FunctionInput {
    let sig = &a.func.sig;
    
    let param = |p: &Param| crash_resume::ParamInfo {
        name: p.name.clone(),
        inplace: p.inplace,
        value: !p.by_ref.contains('@') && (p.by_ref.len() >= 2 || crash_resume::value_type(&p.ty)),
    };
    crash_resume::FunctionInput {
        name: sig.name.clone(),
        reads: sig.params.iter().map(param).collect(),
        writes: sig.returns.iter().map(param).collect(),
        steps: a
            .seq
            .iter()
            .zip(&a.slot_types)
            .map(|(inst, slot_types)| {
                let (op, reads) = inst.flat();
                let callee = step_callee(inst, &op, unit, ext);
                crash_resume::StepInput {
                    op,
                    reads,
                    writes: inst.writes.clone(),
                    write_types: slot_types.clone(),
                    callee,
                }
            })
            .collect(),
        label_starts: a
            .labels
            .values()
            .map(|&start| (start.max(1) - 1) as usize)
            .collect(),
        local_types: local_slot_types(a),
    }
}

fn analyze_file(
    src: &str,
    store: &mut dyn FnMut(&str) -> Option<StoreCallee>,
) -> Result<CheckedFile, String> {
    let (file, mut diags) = parser::parse_code(src)?;
    let structs = collect_struct_table(&file);
    for f in subscript_check_funcs(&file) {
        diags.extend(lower::check_container_subscript(&f, &structs));
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
    let structs = collect_struct_table(&file);
    let unit = UnitNames::new(&file);
    let mut ext = Externals {
        lookup: store,
        cache: HashMap::new(),
    };
    
    let facts: Vec<lower::InplaceFacts> = file.funcs.iter().map(lower::inplace_facts).collect();
    let mut credit: HashMap<String, lower::InplaceCallee> = HashMap::new();
    for name in facts.iter().flat_map(|f| f.callees()) {
        if !credit.contains_key(name) {
            credit.insert(name.to_string(), inplace_callee(name, &unit, &mut ext));
        }
    }
    let mut inplace_diags = lower::check_inplace_unit(&facts, &|name| {
        credit
            .get(name)
            .cloned()
            .unwrap_or(lower::InplaceCallee::Unknown)
    })
    .into_iter();
    let mut funcs: Vec<AnalyzedFunc> = Vec::new();
    for func in &file.funcs {
        let pkg = if func.pkg.is_empty() {
            file.package.clone()
        } else {
            func.pkg.clone()
        };
        let own = inplace_diags.next().unwrap_or_default();
        funcs.push(analyze_func(func, pkg, &structs, &sigs, own)?);
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
        top_init = Some(analyze_func(
            &init_fn,
            String::new(),
            &structs,
            &sigs,
            Vec::new(),
        )?);
    }
    let inputs: Vec<crash_resume::FunctionInput> = funcs
        .iter()
        .chain(top_init.iter())
        .map(|a| crash_resume_input(a, &unit, &mut ext))
        .collect();
    let verdicts = crash_resume::analyze_unit(&inputs);
    for (a, verdict) in funcs.iter_mut().chain(top_init.iter_mut()).zip(verdicts) {
        a.crash_resume = Some(verdict);
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
        funcs,
        top_init,
    })
}

fn analyze_func(
    func: &Func,
    pkg: String,
    structs: &lower::StructTable,
    sigs: &lower::SigTable,
    inplace_diags: Vec<crate::scanner::Diagnostic>,
) -> Result<AnalyzedFunc, String> {
    
    let mut src = func.clone();
    
    let annotated = lower::declared_annotated_names(&src);
    lower::annotate_call_returns(&mut src, sigs);
    
    let mut shape_diags = lower::check_call_shapes(&src, sigs);
    
    shape_diags.extend(inplace_diags);
    lower::check_for_shadowing(&src, &mut shape_diags);
    let mut fn_ = lower::lower_func(&src, structs)?;
    let type_map = lower::infer_types(&fn_, structs);
    
    let declared = lower::declared_langtypes_by_origin(&fn_);
    let type_sets = lower::infer_type_sets(&fn_, structs);
    
    lower::annotate_position_slot_types(&mut fn_, &type_map);
    let mut diags = std::mem::take(&mut shape_diags);
    diags.extend(lower::validate_writes(
        &fn_, &type_map, structs, sigs, &annotated,
    ));
    
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
    
    let mut slot_types: Vec<Vec<String>> = Vec::with_capacity(seq.len());
    let mut slot_coord: HashMap<String, String> = HashMap::new();
    for (index, parameter) in fn_.sig.params.iter().enumerate() {
        slot_coord.insert(parameter.name.clone(), format!("*[0,-{}]", index + 1));
    }
    for (index, parameter) in fn_.sig.returns.iter().enumerate() {
        slot_coord.insert(parameter.name.clone(), format!("*[0,{}]", index + 1));
    }
    annotate_slot_types(
        &mut seq,
        &declared,
        &type_sets,
        &mut stable,
        sigs,
        structs,
        &slot_coord,
    );
    
    {
        let source_langtypes = lower::SourceLangtypes::new(&fn_.sig);
        let mut declared_source = source_langtypes.merged_under(&declared);
        for (name, kind) in &stable {
            if !kind.is_empty() && !kind.contains('|') {
                declared_source
                    .entry(name.clone())
                    .or_insert_with(|| kind.clone());
            }
        }
        let param_types: Vec<String> = fn_.sig.langtype_list();
        let mut param_langtype: HashMap<String, String> = HashMap::new();
        for (index, parameter) in fn_.sig.params.iter().enumerate() {
            param_langtype.insert(parameter.name.clone(), param_types[index].clone());
        }
        for (index, result) in fn_.sig.returns.iter().enumerate() {
            let offset = fn_.sig.num_reads() as usize + index;
            param_langtype.insert(result.name.clone(), param_types[offset].clone());
        }
        insert_write_casts(
            &mut seq,
            |inst, ordinal, slot| {
                write_slot_type(
                    inst,
                    ordinal,
                    slot,
                    &param_langtype,
                    &source_langtypes,
                    &declared,
                    &declared_source,
                    &type_sets,
                    structs,
                    &stable,
                )
            },
            &declared,
            &stable,
        );
        
        for inst in &seq {
            slot_types.push(
                (0..inst.writes.len())
                    .map(|j| {
                        write_slot_type(
                            inst,
                            j,
                            &inst.writes[j],
                            &param_langtype,
                            &source_langtypes,
                            &declared,
                            &declared_source,
                            &type_sets,
                            structs,
                            &stable,
                        )
                    })
                    .collect(),
            );
        }
    }
    
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
        slot_types,
        crash_resume: None,
    })
}

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

fn init_fn_name(pkg: &str) -> String {
    if pkg.is_empty() {
        "init".to_string()
    } else {
        format!("{pkg}{}init", keytree::MEMBER_SEP)
    }
}

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

pub fn vet(src: &str) -> Result<(), String> {
    
    analyze_file(src, &mut |_| None).map(|_| ())
}

pub fn vet_with_store(kv: &mut Kv, src: &str) -> Result<(), String> {
    let checked = analyze_file(src, &mut |name| store_callee(kv, name))?;
    
    let mut local_funcs = unit_func_names(&checked.file);
    local_funcs.extend(
        checked
            .file
            .rwir_decls
            .iter()
            .map(|d| qualify_name(&d.pkg, &d.sig.name)),
    );
    for a in &checked.funcs {
        check_call_names(kv, &a.pkg, &a.seq, &local_funcs)?;
    }
    if let Some(a) = &checked.top_init {
        check_call_names(kv, "", &a.seq, &local_funcs)?;
    }
    Ok(())
}

fn qualify_name(pkg: &str, name: &str) -> String {
    if pkg.is_empty() {
        name.to_string()
    } else {
        format!("{pkg}{}{name}", keytree::MEMBER_SEP)
    }
}

fn unit_func_names(file: &ast::File) -> HashSet<String> {
    file.funcs
        .iter()
        .map(|f| {
            let pkg = if f.pkg.is_empty() {
                &file.package
            } else {
                &f.pkg
            };
            qualify_name(pkg, &f.sig.name)
        })
        .collect()
}

pub fn printlib(kv: &mut Kv, lib: &str) -> String {
    
    let prefix = lib.trim_end_matches('/').to_string();
    let mut funcs: Vec<DumpFunc> = Vec::new();
    let mut decls: Vec<String> = Vec::new();
    if prefix == "/lib" {
        collect_funcs(kv, "/lib/", "", &mut funcs, &mut decls);
    } else if is_func_dir(kv, &format!("{prefix}/")) {
        
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
        
        collect_funcs(kv, "/lib/", "", &mut funcs, &mut decls);
        let under = |dir: &str| {
            dir == prefix
                || dir.starts_with(&format!("{prefix}·"))
                || dir.starts_with(&format!("{prefix}/"))
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

struct DumpFunc {
    pkg: String,
    name: String,
    text: String,
    slots: Vec<String>,
    dir: String,
}

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

fn collect_funcs(
    kv: &mut Kv,
    prefix: &str,
    pkg: &str,
    funcs: &mut Vec<DumpFunc>,
    decls: &mut Vec<String>,
) {
    for c in kv.list(prefix, false, true) {
        if c.ends_with(keytree::SRC_EXT) {
            
            continue;
        }
        let base = c.trim_end_matches('/').to_string();
        let sub_pkg = if pkg.is_empty() {
            base.clone()
        } else {
            format!("{pkg}/{base}")
        };
        
        let dir_sub = format!("{prefix}{base}/");
        let mem_sub = format!("{prefix}{base}·");
        let key = format!("{prefix}{base}");
        let v = kv.get_one(&key);
        let is_rwir_decl =
            !v.is_empty() && kvkind::kind(&v, &kv.langtype_of(&key)) == kvkind::KIND_DEF_RWIR;
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
        } else if is_rwir_decl {
            decls.push(format!(
                "{key} {}",
                sanitize(&kvkind::display(&v, &kv.langtype_of(&key)))
            ));
            for slot in kv.list(&dir_sub, false, true) {
                if !slot.ends_with('/') {
                    let skey = format!("{dir_sub}{slot}");
                    let sv = kv.get_one(&skey);
                    decls.push(format!(
                        "{skey} {}",
                        sanitize(&kvkind::display(&sv, &kv.langtype_of(&skey)))
                    ));
                }
            }
        } else if !kv.list(&dir_sub, false, true).is_empty() {
            collect_funcs(kv, &dir_sub, &sub_pkg, funcs, decls);
        } else if !kv.list(&mem_sub, false, true).is_empty() {
            collect_funcs(kv, &mem_sub, &sub_pkg, funcs, decls);
        } else {
            decls.push(format!(
                "{key} {}",
                sanitize(&kvkind::display(&v, &kv.langtype_of(&key)))
            ));
        }
    }
}

fn is_func_dir(kv: &mut Kv, dir: &str) -> bool {
    !kv.get_one(&format!("{dir}[0,0]")).is_empty()
}

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

fn collect_slots(kv: &mut Kv, prefix: &str, out: &mut Vec<String>) {
    for c in kv.list(prefix, false, true) {
        let full = format!("{prefix}{c}");
        if c.ends_with('/') {
            collect_slots(kv, &full, out);
        } else {
            let v = kv.get_one(&full);
            out.push(format!(
                "{c} {}",
                sanitize(&kvkind::display(&v, &kv.langtype_of(&full)))
            ));
        }
    }
}

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

fn reconstruct_sig(kv: &mut Kv, dir: &str, name: &str, nr: i32, nw: i32, dynamic: bool) -> FuncSig {
    let blank = || Param {
        name: String::new(),
        ty: String::new(),
        by_ref: String::new(),
        has_ty: false,
        inplace: false,
    };
    let mut params: Vec<Param> = (0..nr).map(|_| blank()).collect();
    let mut returns: Vec<Param> = (0..nw).map(|_| blank()).collect();
    let base = dir.trim_end_matches('/');
    for k in 1..=nr {
        if let Some((pty, _)) = kvkind::def_param_full(&kv.get_one(&format!("{base}.[0,-{k}]"))) {
            params[(k - 1) as usize] = Param {
                name: String::new(),
                ty: pty,
                by_ref: String::new(),
                has_ty: true,
                inplace: false,
            };
        }
    }
    for k in 1..=nw {
        if let Some((rty, r_inplace)) =
            kvkind::def_param_full(&kv.get_one(&format!("{base}.[0,{k}]")))
        {
            returns[(k - 1) as usize] = Param {
                name: String::new(),
                ty: rty,
                by_ref: String::new(),
                has_ty: true,
                inplace: r_inplace,
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

struct RawInst {
    opcode: String,
    reads: Vec<Operand>,
    writes: Vec<String>,
    write_types: Vec<String>,
}

enum Operand {
    Ref(String),
    Str(String),
    Lit(String),
}

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

fn build_body(insts: &[RawInst], labels: &[(i32, String)]) -> Vec<Stmt> {
    let by_irseq: HashMap<i32, String> = labels.iter().map(|(i, l)| (*i, l.clone())).collect();
    let total = insts.len() as i32;
    
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

fn build_inst(inst: &RawInst, by_irseq: &HashMap<i32, String>) -> Instruction {
    let expr = match inst.opcode.as_str() {
        "" => None,
        "return" => Some(ast::leaf("return")),
        
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
        pos: Pos::default(),
    }
}

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

pub fn write_func(
    kv: &mut Kv,
    a: &AnalyzedFunc,
    local_funcs: &HashSet<String>,
    structs: &lower::StructTable,
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

    let source_langtypes = lower::SourceLangtypes::new(&fn_.sig);
    let mut declared_source = source_langtypes.merged_under(declared);
    for (name, kind) in stable {
        if !kind.is_empty() && !kind.contains('|') {
            declared_source
                .entry(name.clone())
                .or_insert_with(|| kind.clone());
        }
    }

    let ctime_key = format!("{func_dir}/‥ctime");
    let ctime = next_ctime(read_ctime(kv, &ctime_key));

    let nr = fn_.sig.num_reads();
    let nw = fn_.sig.num_writes();
    let param_types: Vec<String> = fn_.sig.langtype_list();
    
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
    
    for (i, _) in fn_.sig.params.iter().enumerate() {
        pairs.push((
            format!("{func_dir}.[0,-{}]", i + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_param(&param_types[i])?,
        ));
    }
    for (i, r) in fn_.sig.returns.iter().enumerate() {
        pairs.push((
            format!("{func_dir}.[0,{}]", i + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_param_with_inplace(&param_types[nr as usize + i], r.inplace)?,
        ));
    }

    let werr = |e: String| format!("pathspace write {func_dir}: {e}");
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
        )
        .map_err(werr)?;
    }
    
    #[cfg(feature = "layout-stats")]
    {
        let (mut concrete, mut union, mut unknown) = (0usize, 0usize, 0usize);
        
        let mut per_op: std::collections::BTreeMap<
            String,
            (
                std::collections::BTreeSet<String>,
                std::collections::BTreeSet<String>,
            ),
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
                    write_slot_type(
                        inst,
                        j,
                        w,
                        &param_langtype,
                        &source_langtypes,
                        declared,
                        &declared_source,
                        type_sets,
                        structs,
                        stable,
                    ),
                    true,
                );
            }
        }
        eprintln!(
            "layout-stats: {opcode}: operands={} concrete={concrete} union={union} unknown={unknown}",
            concrete + union + unknown
        );
        for (op, (reads, writes)) in &per_op {
            
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
            
            lpairs.push((
                keytree::lib_label(pkg, &fn_.sig.name, label),
                "int64".to_string(),
                ffi::new_int64(*irseq as i64)?,
            ));
        }
        kv.set(&lpairs).map_err(werr)?;
    }
    
    if let Some(resumability) = &a.crash_resume {
        let (text, langtype) = crash_resume_record(resumability);
        let key = format!(
            "{func_dir}/{}{}",
            keytree::RUNTIME_MEMBER_SEP,
            keytree::SEG_CRASH_RESUME
        );
        kv.set(&[(key, langtype, ffi::new_char_byte(text.as_bytes())?)])
            .map_err(werr)?;
    }
    
    kv.set(&[(ctime_key, String::new(), ffi::new_int64(ctime)?)])
        .map_err(werr)?;
    Ok(())
}

fn crash_resume_record(resumability: &crash_resume::Resumability) -> (String, String) {
    (resumability.to_string(), "[]char/utf8".to_string())
}

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
        
        if fld.ty.starts_with('*') {
            continue;
        }
        pairs.push((
            keytree::member(&base, &fld.name),
            fld.ty.clone(),
            field_default(&fld.ty, fld.default.as_ref())?,
        ));
    }
    let werr = |e: String| format!("pathspace write {base}: {e}");
    kv.del_tree(&base).map_err(werr)?;
    kv.set(&pairs).map_err(werr)
}

fn field_default(ty: &str, default: Option<&Expr>) -> Result<Vec<u8>, String> {
    
    let (dims, base) = kvkind::parse_langtype(ty);
    let s = default.map(|e| e.val.clone()).unwrap_or_default();
    if base.starts_with("char/") {
        return ffi::new_char(&base, &s);
    }
    if !dims.is_empty() {
        return ffi::xvalue_to_wire(&base, &[], dims.iter().product());
    }
    
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
            kvkind::new_def_langtype_with_inplace(
                &param_types[nr as usize + i],
                decl.sig.returns[i].inplace,
            )?,
        ));
    }
    kv.set(&pairs)
        .map_err(|e| format!("pathspace write {base}: {e}"))
}

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
                
                let terminated =
                    (seq.len() as i32) >= start && seq.last().is_some_and(inst_is_terminator);
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

fn to_wire(ty: &str) -> String {
    ty.trim_end_matches("...")
        .trim_start_matches(['*', '@'])
        .to_string()
}

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
    
    if !lower::slot_is_plain_name(name) {
        
        let resolved = lower::resolve_slot_type(name, declared_source, structs, declared_source)
            .or_else(|| lower::resolve_slot_type(name, declared, structs, declared));
        if let Some(resolved) = resolved {
            let wire = to_wire(&resolved);
            if !wire.is_empty() {
                return narrow_union(wire, name, stable);
            }
        }
    }
    
    let (lookup_name_owned, is_deref) = lower::normalize_slot_name(name);
    let lookup_name = lookup_name_owned.as_str();
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
    
    if let Some(t) = source_langtypes.langtype_wire(lookup_name) {
        if !t.is_empty() {
            return narrow_union(t, lookup_name, stable);
        }
    }
    if let Some(t) = param_langtype.get(lookup_name).filter(|t| !t.is_empty()) {
        return narrow_union(t.clone(), lookup_name, stable);
    }
    
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

fn annotate_slot_types(
    seq: &mut [Instruction],
    declared: &HashMap<String, String>,
    type_sets: &HashMap<String, Vec<String>>,
    stable: &mut HashMap<String, String>,
    sigs: &lower::SigTable,
    structs: &lower::StructTable,
    coord: &HashMap<String, String>,
) {
    
    for inst in seq.iter_mut() {
        if inst.writes.is_empty() {
            continue;
        }
        let Some(expr) = inst.expr.as_ref() else {
            continue;
        };
        
        let ty = if expr.is_leaf() {
            
            let resolved = lower::resolve_slot_type(&expr.val, declared, structs, declared)
                .or_else(|| lower::resolve_slot_type(&expr.val, stable, structs, stable));
            resolved.unwrap_or_default()
        } else {
            expr_slot_type(expr, declared, sigs, stable, structs, coord).unwrap_or_default()
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

fn expr_slot_type(
    expr: &ast::Expr,
    declared: &HashMap<String, String>,
    sigs: &lower::SigTable,
    derived: &HashMap<String, String>,
    structs: &lower::StructTable,
    coord: &HashMap<String, String>,
) -> Option<String> {
    
    if expr.op == ast::DEREF_OP || expr.op == ast::MEMBER_READ_OP {
        return value_type_of(expr, declared, derived, structs, coord);
    }
    
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
    
    if lower::is_cast_op(&expr.op) || expr.op.starts_with("char/") {
        return Some(expr.op.clone());
    }
    
    let Some(sig) = lower::sig_lookup(sigs, &expr.op) else {
        let argument_types: Vec<Option<String>> = expr
            .args
            .iter()
            .map(|argument| value_type_of(argument, declared, derived, structs, coord))
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
        
        return None;
    }
    Some(first.ty.clone())
}

fn value_type_of(
    expr: &ast::Expr,
    declared: &HashMap<String, String>,
    derived: &HashMap<String, String>,
    structs: &lower::StructTable,
    coord: &HashMap<String, String>,
) -> Option<String> {
    if expr.op == ast::DEREF_OP {
        let inner = expr.args.first()?;
        
        let (name, _) = lower::normalize_slot_name(&inner.val);
        let ty = value_type_of(
            &ast::Expr {
                val: name,
                ..inner.clone()
            },
            declared,
            derived,
            structs,
            coord,
        )?;
        return Some(lower::deref_value_type(&ty).to_string());
    }
    if expr.op == ast::MEMBER_READ_OP {
        let base = expr.args.first()?;
        let base_type = value_type_of(base, declared, derived, structs, coord)?;
        let tail: Vec<&str> = expr.args.iter().skip(1).map(|a| a.val.as_str()).collect();
        let member = tail.join(keytree::MEMBER_SEP);
        return lower::member_chain_type(&base_type, &member, structs)
            
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
    
    if let Some(name) = coord
        .iter()
        .find(|(_, coordinate)| coordinate.as_str() == expr.val)
        .map(|(name, _)| name.clone())
    {
        return declared
            .get(&name)
            .cloned()
            .or_else(|| derived.get(&name).cloned());
    }
    declared
        .get(&expr.val)
        .or_else(|| derived.get(&expr.val))
        .cloned()
}

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
    
    let (base_name, _) = lower::normalize_slot_name(&base.val);
    let base_type = declared
        .get(&base.val)
        .or_else(|| declared.get(&base_name))
        .or_else(|| stable.get(&base.val))
        .or_else(|| stable.get(&base_name))?;
    let field = e.args.get(1)?;
    
    let bare_type = base_type.trim_start_matches('*');
    let name = bare_type.strip_prefix("/lib/").unwrap_or(bare_type);
    structs
        .get(name)?
        .fields
        .iter()
        .find(|f| f.name == field.val)
        .map(|f| f.ty.clone())
        .filter(|t| !t.is_empty())
}

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

fn narrow_union(ty: String, name: &str, stable: &HashMap<String, String>) -> String {
    if !ty.contains('|') {
        return ty;
    }
    match stable.get(name).filter(|k| !k.contains('|')) {
        Some(k) if lower::type_members(&ty).contains(&k.as_str()) => k.clone(),
        _ => ty,
    }
}

fn write_scalar_kind(resolved: &str) -> Option<String> {
    let ty = resolved.trim();
    if ty.is_empty()
        || ty.contains('|')
        || ty.starts_with('*')
        || ty.starts_with('@')
        || ty.starts_with('[')
        || ty.contains(keytree::MEMBER_SEP)
        || ty.contains('/')
    {
        return None;
    }
    builtin::is_num_kind(ty).then(|| ty.to_string())
}

fn insert_write_casts(
    seq: &mut [Instruction],
    resolve_write_type: impl Fn(&Instruction, usize, &str) -> String,
    declared: &HashMap<String, String>,
    stable: &HashMap<String, String>,
) {
    for inst in seq.iter_mut() {
        let Some(expr) = inst.expr.take() else {
            continue;
        };
        
        if expr.op == "compactarray·set" {
            inst.expr = Some(expr);
            continue;
        }
        let slot = match inst.writes.as_slice() {
            [slot] => slot.clone(),
            _ => {
                inst.expr = Some(expr);
                continue;
            }
        };
        let resolved = resolve_write_type(inst, 0, &slot);
        let Some(target_kind) = write_scalar_kind(&resolved) else {
            inst.expr = Some(expr);
            continue;
        };
        let source_kind = lower::exact_numeric_kind(&expr, declared, stable);
        
        if source_kind.as_deref() == Some(target_kind.as_str()) {
            inst.expr = Some(expr);
            continue;
        }
        
        if source_kind.is_none() {
            inst.expr = Some(expr);
            continue;
        }
        inst.expr = Some(lower::wrap_cast(&expr, &target_kind));
    }
}

#[allow(clippy::too_many_arguments)]

fn param_slot<'a>(params: &'a HashMap<String, String>, text: &'a str) -> &'a str {
    if let Some(value) = params.get(text) {
        return value.as_str();
    }
    
    let (bare, dereferenced) = lower::normalize_slot_name(text);
    if dereferenced {
        if let Some(value) = params.get(&bare) {
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
) -> Result<(), String> {
    let (opcode, mut reads) = s.flat();
    
    let opcode = match builtin::output_opid(&opcode) {
        Some(identifier) => identifier.to_string(),
        None => opcode,
    };
    
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
    
    let none_index = reads.iter().position(|operand| operand == "None");
    if let Some(index) = none_index {
        if opcode == "==" || opcode == "!=" {
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
            
            if !lower::is_none_comparable(&other_type) {
                return Err(format!(
                    "{opcode}: 无法与 None 比较——另一侧 `{other}` 的类型 `{other_type}` 不在判空能力面内"
                ));
            }
        }
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
        
        if let Some((wire, lt)) = adapted_literal_slot(rv, &s.operand_kind)? {
            pairs.push((format!("{prefix}/[{n},-{}]", j + 1), lt, wire));
            continue;
        }
        if Some(j) == none_index {
            
            let wire = ffi::new_none()?;
            let other_index = if j == 0 { 1 } else { 0 };
            let other_type = reads
                .get(other_index)
                .map(|other| {
                    read_slot_type(
                        other,
                        param_langtype,
                        source_langtypes,
                        declared,
                        declared_source,
                        type_sets,
                        structs,
                        stable,
                    )
                })
                .unwrap_or_default();
            let slot_type = if lower::is_none_comparable(&other_type)
                && (other_type.starts_with('*') || other_type.starts_with('/'))
            {
                other_type
            } else {
                String::new()
            };
            pairs.push((format!("{prefix}/[{n},-{}]", j + 1), slot_type, wire));
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
        let ty = write_slot_type(
            s,
            j,
            orig,
            param_langtype,
            source_langtypes,
            declared,
            declared_source,
            type_sets,
            structs,
            stable,
        );
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
        
        if !pkg.is_empty() {
            let qual = format!("{pkg}{}{op}", keytree::MEMBER_SEP);
            let qdir = format!("/lib/{qual}/");
            if local_funcs.contains(&qual)
                || kvkind::kind(&kv.get_one(&qdir), &kv.langtype_of(&qdir)) == kvkind::KIND_RWFUNC
            {
                return Err(undefined_call(&op));
            }
        }
        
        if is_operator_word(&op) {
            continue;
        }
        
        return Err(undefined_call(&op));
    }
    Ok(())
}

fn undefined_call(op: &str) -> String {
    format!("未定义函数 {op}：未给出被调函数的完整路径（写 `包名·函数名` 或 `/lib/包名·函数名`）")
}

fn is_operator_word(op: &str) -> bool {
    crate::builtin::num_op(op) || matches!(op, "and" | "or" | "not") || is_cast_name(op)
}

fn is_cast_name(op: &str) -> bool {
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
            | "bool"
            | "char/utf8"
            | "char/utf32"
            | "char/ascii"
    ) || op == "stringkeymap"
}

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
    
    if is_operator {
        return Ok((
            kvkind::new_rwir(nr, nw, opcode)?,
            kvkind::KIND_RWIR.to_string(),
        ));
    }
    
    let name = opcode.strip_prefix("/lib/").unwrap_or(opcode);
    let is_func = local_funcs.contains(name)
        || kvkind::kind(
            &kv.get_one(&format!("/lib/{name}/")),
            &kv.langtype_of(&format!("/lib/{name}/")),
        ) == kvkind::KIND_RWFUNC;
    Ok(if is_func {
        
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

fn integer_literal_text(s: &str) -> bool {
    let b = s.as_bytes();
    let start = usize::from(b.first() == Some(&b'-'));
    let rest = &b[start..];
    !rest.is_empty() && rest.iter().all(u8::is_ascii_digit)
}

fn adapted_literal_slot(text: &str, kind: &str) -> Result<Option<(Vec<u8>, String)>, String> {
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

fn slot_value(val: &str, target_char: &str) -> Result<(Vec<u8>, String), String> {
    
    if val == "None" {
        return Ok((ffi::new_none()?, String::new()));
    }
    if !is_literal(val) {
        return Ok((kvkind::new_rwir(0, 0, val)?, kvkind::KIND_RWIR.to_string()));
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

fn clip(s: String) -> String {
    let n = s.chars().count();
    if n <= 200 {
        return s;
    }
    let mut out: String = s.chars().take(200).collect();
    out.push('…');
    out
}

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

pub fn printstack(kv: &mut Kv, vid: &str) -> String {
    if vid.is_empty() {
        return "error: printstack requires vid".to_string();
    }
    let root = format!("/vthread/{vid}");
    
    let status = plain_of(kv, &format!("{root}/‥status"));
    
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
        
        for key in ["‥callpc", "‥returnpc"] {
            let full = format!("{f}/{key}");
            if kv.get_one(&full).is_empty() {
                continue; 
            }
            match pc_of(kv, &full) {
                Ok((stackid, s0)) => {
                    out.push_str(&format!("  {key} = stackid={stackid} s0={s0}\n"))
                }
                
                Err(message) => out.push_str(&format!("  {key} = error: {message}\n")),
            }
        }
        
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
                continue; 
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
                    if !kvkind::is_ptr(&v, &kv.langtype_of(&cur_key)) {
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
        roundtrip("lib http {\n\trwfunc get(*[]char/utf32 #url) -> (*[]char/utf32 #resp) {\n\t\thttp·call(\"GET\", \"\", *url, \"\") -> *resp\n\t}\n}\n");
        roundtrip("lib pathseek {\nlib session {\nlib s1 {\nrwfunc main() -> () {\ns:int64 = 3 + 4\nprintln(s)\n}\n}\n}\n}\nmain()\n");
    }

    #[test]
    fn null_literal_rejected() {
        
        assert!(vet("lib t {\nrwfunc main() -> () {\nnull -> x\n}\n}\n").is_err());
        assert!(vet("lib t {\nrwfunc main() -> () {\nprintln(null)\n}\n}\n").is_err());
        
        assert!(vet("lib t {\nrwfunc main() -> () {\nx:*int64 = None\n}\n}\n").is_ok());
        assert!(vet("lib t {\nrwfunc main() -> () {\nx:[]char/utf32 = \"null\"\n}\n}\n").is_ok());
    }

    #[test]
    fn nested_lib_layout_and_merge() {
        let dsn_nested = format!("fs:///tmp/pathlanglayout_nested_{}", std::process::id());
        
        struct Guard_nested(String);
        impl Drop for Guard_nested {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard_nested = Guard_nested(dsn_nested.clone());
        let _ = std::fs::remove_dir_all(dsn_nested.trim_start_matches("fs://"));
        
        if crate::ffi::try_connect(&dsn_nested).is_none() {
            eprintln!(
                "SKIP: 后端不可用（{}）——本期 libpathspace 只服务 shm://，这是能力边界不是回归",
                dsn_nested
            );
            return;
        }
        let mut kv = Kv::conn(&dsn_nested);
        init_dirs(&mut kv).unwrap();

        compile(
            &mut kv,
            "lib a {\nlib b {\nrwfunc f() -> (int64 #r) {\n*r = 1\n}\n}\n}\n",
        )
        .unwrap();
        let dir = kv.get_one("/lib/a/b·f/");
        assert_eq!(kvkind::kind(&dir, &kv.langtype_of("/lib/a/b·f/")), "rwfunc");
        
        assert_eq!(ffi::xh_len(&dir), 0);
        assert_eq!(
            kvkind::kind(
                &kv.get_one("/lib/a/b·f/[0,0]"),
                &kv.langtype_of("/lib/a/b·f/[0,0]")
            ),
            "rwfunc"
        );
        
        let anchor = kv.get_one("/lib/a/b·f/[0,0]");
        assert!(!kvkind::body(&anchor).is_empty());

        compile(
            &mut kv,
            "lib a {\nlib c {\nrwfunc g() -> (int64 #g) {\n*g = 2\n}\n}\n}\n",
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
            kvkind::kind(
                &kv.get_one("/lib/a/c·g/[0,0]"),
                &kv.langtype_of("/lib/a/c·g/[0,0]")
            ),
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
        let dsn_call_kind = format!("fs:///tmp/pathlanglayout_call_kind_{}", std::process::id());
        
        struct Guard_call_kind(String);
        impl Drop for Guard_call_kind {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard_call_kind = Guard_call_kind(dsn_call_kind.clone());
        let _ = std::fs::remove_dir_all(dsn_call_kind.trim_start_matches("fs://"));
        
        if crate::ffi::try_connect(&dsn_call_kind).is_none() {
            eprintln!(
                "SKIP: 后端不可用（{}）——本期 libpathspace 只服务 shm://，这是能力边界不是回归",
                dsn_call_kind
            );
            return;
        }
        let mut kv = Kv::conn(&dsn_call_kind);
        init_dirs(&mut kv).unwrap();
        
        kv.set(&[(
            "/lib/println".to_string(),
            kvkind::KIND_DEF_RWIR.to_string(),
            kvkind::new_defrwir(1, 0, false).unwrap(),
        )])
        .unwrap();
        compile(&mut kv, "rwfunc caller() -> () {\ny:int64 = callee(1)\nprintln(y)\n}\nrwfunc callee(int64 #x) -> (int64 #r) {\nx -> *r\n}\n").unwrap();
        assert_eq!(
            kvkind::kind(
                &kv.get_one("/lib/caller/[1,0]"),
                &kv.langtype_of("/lib/caller/[1,0]")
            ),
            "rwfunc"
        );
        assert_eq!(
            kvkind::counts(&kv.get_one("/lib/caller/[1,0]")),
            (1, 1, false)
        );
        assert_eq!(
            kvkind::kind(
                &kv.get_one("/lib/caller/[2,0]"),
                &kv.langtype_of("/lib/caller/[2,0]")
            ),
            "rwir"
        );
        assert_eq!(
            kvkind::counts(&kv.get_one("/lib/caller/[2,0]")),
            (1, 0, false)
        );
        compile(&mut kv, "rwfunc later() -> () {\ny:int64 = callee(1)\n}\n").unwrap();
        assert_eq!(
            kvkind::kind(
                &kv.get_one("/lib/later/[1,0]"),
                &kv.langtype_of("/lib/later/[1,0]")
            ),
            "rwfunc"
        );
        compile(
            &mut kv,
            "rwfunc absolute() -> () {\ny:int64 = /lib/callee(1)\n}\n",
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
        let dsn_struct = format!("fs:///tmp/pathlanglayout_struct_{}", std::process::id());
        
        struct Guard_struct(String);
        impl Drop for Guard_struct {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard_struct = Guard_struct(dsn_struct.clone());
        let _ = std::fs::remove_dir_all(dsn_struct.trim_start_matches("fs://"));
        
        if crate::ffi::try_connect(&dsn_struct).is_none() {
            eprintln!(
                "SKIP: 后端不可用（{}）——本期 libpathspace 只服务 shm://，这是能力边界不是回归",
                dsn_struct
            );
            return;
        }
        let mut kv = Kv::conn(&dsn_struct);
        init_dirs(&mut kv).unwrap();
        compile(
            &mut kv,
            "struct Point {\n\tx:int64=3\n\tnext:*Point=None\n}\n",
        )
        .unwrap();
        let proto = kv.get_one("/lib/Point");
        assert_eq!(
            kvkind::kind(&proto, &kv.langtype_of("/lib/Point")),
            "def struct"
        );
        assert!(kvkind::body(&proto).is_empty());
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
        
        let dsn_dot = format!("fs:///tmp/pathlanglayout_dot_{}", std::process::id());
        
        struct Guard_dot(String);
        impl Drop for Guard_dot {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard_dot = Guard_dot(dsn_dot.clone());
        let _ = std::fs::remove_dir_all(dsn_dot.trim_start_matches("fs://"));
        
        if crate::ffi::try_connect(&dsn_dot).is_none() {
            eprintln!(
                "SKIP: 后端不可用（{}）——本期 libpathspace 只服务 shm://，这是能力边界不是回归",
                dsn_dot
            );
            return;
        }
        let mut kv = Kv::conn(&dsn_dot);
        init_dirs(&mut kv).unwrap();
        
        kv.set(&[(
            "/lib/baz".to_string(),
            kvkind::KIND_DEF_RWIR.to_string(),
            kvkind::new_defrwir(1, 1, false).unwrap(),
        )])
        .unwrap();
        compile(
            &mut kv,
            "lib t {\nrwfunc main() -> () {\ny:int64 = foo.bar(\"x\")\nw:int64 = baz(\"z\")\n}\n}\n",
        )
        .unwrap();
        assert_eq!(
            kvkind::kind(
                &kv.get_one("/lib/t·main/[0,0]"),
                &kv.langtype_of("/lib/t·main/[0,0]")
            ),
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
        
        let dsn = format!("fs:///tmp/pathlanglayout_named_block_{}", std::process::id());
        struct Guard(String);
        impl Drop for Guard {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
            }
        }
        let _guard = Guard(dsn.clone());
        let _ = std::fs::remove_dir_all(dsn.trim_start_matches("fs://"));
        
        if crate::ffi::try_connect(&dsn).is_none() {
            eprintln!(
                "SKIP: 后端不可用（{}）——本期 libpathspace 只服务 shm://，这是能力边界不是回归",
                dsn
            );
            return;
        }
        let mut kv = Kv::conn(&dsn);
        init_dirs(&mut kv).unwrap();
        compile(
            &mut kv,
            "lib t {\nrwfunc main() -> () {\nx:int64 = 1\ninner: {\nx = 2\n}\nx = 3\n}\n}\n",
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
