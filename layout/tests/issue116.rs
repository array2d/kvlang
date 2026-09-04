//! Control-flow after lower: one [irseq,j] plane, goto/br to int64, no scope keys.
use std::collections::{BTreeMap, HashMap};

use kvlanglayout::{compile, init_dirs, kvkind, Kv};

fn body(data: &[u8]) -> &[u8] {
    let h = kvkind::head(data);
    kvkind::body(data, &h)
}

fn sig(data: &[u8]) -> String {
    let b = body(data);
    String::from_utf8_lossy(&b[4.min(b.len())..]).into_owned()
}

fn slot_text(data: &[u8]) -> String {
    let k = kvkind::kind(data);
    if k == "int64" || k == "bool" || k == "float64" {
        kvkind::display(data).rsplit(':').next().unwrap_or("").to_string()
    } else {
        sig(data)
    }
}

fn fresh_kv() -> Kv {
    let dsn = format!(
        "fs:///tmp/kvlanglayout_i116_{}_{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    );
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    kv
}

fn children_of(kv: &mut Kv, dir: &str) -> Vec<String> {
    kv.list(dir, false, false)
}

fn is_terminator_op(op: &str) -> bool {
    matches!(op, "return" | "goto" | "br")
}

fn parse_coord(name: &str) -> Option<(i32, i32)> {
    let t = name.trim_end_matches('/');
    if !t.starts_with('[') || !t.ends_with(']') || !t.contains(',') {
        return None;
    }
    let inner = &t[1..t.len() - 1];
    let mut it = inner.split(',');
    let row = it.next()?.parse().ok()?;
    let col = it.next()?.parse().ok()?;
    if it.next().is_some() {
        return None;
    }
    Some((row, col))
}

fn parse_row(name: &str) -> Option<i32> {
    let (row, col) = parse_coord(name)?;
    (col == 0).then_some(row)
}

struct Inst {
    op: String,
    reads: Vec<String>,
    writes: Vec<String>,
}

fn plane(kv: &mut Kv, fn_: &str) -> BTreeMap<i32, Inst> {
    let prefix = format!("/lib/{fn_}/");
    let children = children_of(kv, &prefix);
    let mut slots: BTreeMap<(i32, i32), String> = BTreeMap::new();
    for c in &children {
        let Some((row, col)) = parse_coord(c) else {
            continue;
        };
        slots.insert((row, col), slot_text(&kv.get_one(&format!("{prefix}{c}"))));
    }
    let mut out = BTreeMap::new();
    for ((row, col), text) in slots {
        let e = out.entry(row).or_insert_with(|| Inst {
            op: String::new(),
            reads: Vec::new(),
            writes: Vec::new(),
        });
        if col == 0 {
            e.op = text;
        } else if col < 0 {
            let i = ((-col) as usize).saturating_sub(1);
            if e.reads.len() <= i {
                e.reads.resize(i + 1, String::new());
            }
            e.reads[i] = text;
        } else {
            let i = (col as usize).saturating_sub(1);
            if e.writes.len() <= i {
                e.writes.resize(i + 1, String::new());
            }
            e.writes[i] = text;
        }
    }
    out
}

fn dump_ir(p: &BTreeMap<i32, Inst>) -> String {
    let mut s = String::new();
    for (n, inst) in p {
        if *n == 0 {
            continue;
        }
        s.push_str(&format!("{n}: {}", inst.op));
        if !inst.reads.is_empty() {
            s.push(' ');
            s.push_str(&inst.reads.join(" "));
        }
        if !inst.writes.is_empty() {
            s.push_str(" -> ");
            s.push_str(&inst.writes.join(" "));
        }
        s.push('\n');
    }
    s
}

fn labels_of(kv: &mut Kv, fn_: &str) -> HashMap<String, i32> {
    let dir = format!("/lib/{fn_}/\u{2025}labels/");
    let mut out = HashMap::new();
    for name in children_of(kv, &dir) {
        let n = name.trim_end_matches('/');
        if n.is_empty() {
            continue;
        }
        let v = kv.get_one(&format!("{dir}{n}"));
        if kvkind::kind(&v) != "int64" {
            continue;
        }
        let disp = kvkind::display(&v);
        let irseq: i32 = disp
            .split(':')
            .nth(1)
            .unwrap_or_else(|| panic!("label {n} display={disp}"))
            .parse()
            .unwrap_or_else(|_| panic!("label {n} display={disp}"));
        out.insert(n.to_string(), irseq);
    }
    out
}

fn assert_single_plane(kv: &mut Kv, fn_: &str) {
    let children = children_of(kv, &format!("/lib/{fn_}/"));
    assert!(!children.is_empty(), "{fn_}: empty");
    let nested: Vec<_> = children
        .iter()
        .filter(|c| {
            let b = c.trim_end_matches('/');
            b.contains('/') || (b.starts_with('_') && b.contains('['))
        })
        .cloned()
        .collect();
    assert!(nested.is_empty(), "{fn_} nested/scope keys: {nested:?} all={children:?}");
    let dirs: Vec<_> = children
        .iter()
        .filter(|c| c.ends_with('/') && !c.contains("labels") && parse_coord(c).is_none())
        .cloned()
        .collect();
    assert!(dirs.is_empty(), "{fn_} extra dirs: {dirs:?} all={children:?}");
    assert!(children.iter().any(|c| c.starts_with('[')), "{fn_} has no [i,j]: {children:?}");
}

fn assert_jumps_are_int64(kv: &mut Kv, fn_: &str, p: &BTreeMap<i32, Inst>) {
    let mut saw = false;
    for (n, inst) in p {
        if inst.op != "goto" && inst.op != "br" {
            continue;
        }
        saw = true;
        let prefix = format!("/lib/{fn_}/");
        let cols: &[i32] = if inst.op == "goto" { &[-1] } else { &[-2, -3] };
        for col in cols {
            let v = kv.get_one(&format!("{prefix}[{n},{col}]"));
            assert_eq!(
                kvkind::kind(&v),
                "int64",
                "{fn_} [{n},{col}] {} {}",
                inst.op,
                kvkind::display(&v)
            );
            let irseq: i32 = slot_text(&v).parse().unwrap();
            assert!(irseq >= 1, "{fn_} [{n}] {} target {irseq} < 1", inst.op);
            assert!(p.contains_key(&irseq), "{fn_} [{n}] {} -> missing {irseq}", inst.op);
        }
    }
    assert!(saw, "{fn_}: no goto/br in\n{}", dump_ir(p));
}

fn assert_no_scope_opcode(p: &BTreeMap<i32, Inst>, fn_: &str) {
    for (n, inst) in p {
        if *n == 0 {
            continue;
        }
        assert_ne!(inst.op, "scope", "{fn_} [{n}] still has scope opcode");
        assert!(
            !inst.op.starts_with('_'),
            "{fn_} [{n}] opcode looks like a lowered label: {}",
            inst.op
        );
        if inst.op == "call" {
            let target = inst.reads.first().map(String::as_str).unwrap_or("");
            assert!(
                !target.contains("_then_")
                    && !target.contains("_else_")
                    && !target.contains("_while_")
                    && !target.contains("_do_")
                    && !target.contains("_if_")
                    && !target.contains("_merge_")
                    && !target.contains("_for_"),
                "{fn_} [{n}] call still targets a scope: {target}"
            );
        }
    }
}

fn compile_fn(src: &str) -> Kv {
    let mut kv = fresh_kv();
    compile(&mut kv, src).unwrap();
    kv
}

#[test]
fn lib_plane_has_no_scope_flat_keys() {
    let mut kv = fresh_kv();
    let src = std::fs::read_to_string("../tutorial/03-control/while.kv").unwrap();
    compile(&mut kv, &src).unwrap();
    for fn_ in ["sum_to", "first_div7", "sum_odds"] {
        let prefix = format!("/lib/{fn_}/");
        let children = children_of(&mut kv, &prefix);
        assert!(!children.is_empty(), "{fn_}: empty");
        let bad: Vec<_> = children
            .iter()
            .filter(|c| {
                let b = c.trim_end_matches('/');
                b.starts_with('_') && b.contains('[')
            })
            .cloned()
            .collect();
        assert!(bad.is_empty(), "{fn_} still has scope flat keys: {bad:?} all={children:?}");
        assert!(children.iter().any(|c| c.starts_with('[')), "{fn_} has no [i,j]: {children:?}");
        assert!(children.iter().any(|c| c.contains("labels")), "{fn_} missing ‥labels: {children:?}");
    }
}

#[test]
fn labeled_irseq_is_not_fallen_into() {
    let mut kv = fresh_kv();
    let src = std::fs::read_to_string("../tutorial/03-control/guess.kv").unwrap();
    compile(&mut kv, &src).unwrap();
    let fn_ = "guess_number";
    let prefix = format!("/lib/{fn_}/");
    let labels = children_of(&mut kv, &format!("{prefix}\u{2025}labels/"));
    assert!(!labels.is_empty(), "no labels");
    let mut labeled = std::collections::HashSet::new();
    for name in &labels {
        let n = name.trim_end_matches('/');
        if n.is_empty() {
            continue;
        }
        let v = kv.get_one(&format!("{prefix}\u{2025}labels/{n}"));
        if kvkind::kind(&v) != "int64" {
            continue;
        }
        let disp = kvkind::display(&v);
        let irseq: i32 = disp
            .split(':')
            .nth(1)
            .unwrap_or_else(|| panic!("label {n} display={disp}"))
            .parse()
            .unwrap_or_else(|_| panic!("label {n} display={disp}"));
        labeled.insert(irseq);
    }
    let children = children_of(&mut kv, &prefix);
    let mut ops = std::collections::BTreeMap::new();
    for c in &children {
        if let Some(row) = parse_row(c) {
            ops.insert(row, sig(&kv.get_one(&format!("{prefix}{c}"))));
        }
    }
    for irseq in labeled {
        if irseq <= 1 {
            continue;
        }
        let prev = irseq - 1;
        let op = ops.get(&prev).unwrap_or_else(|| panic!("no opcode at [{prev},0] before label {irseq}"));
        assert!(is_terminator_op(op), "irseq {prev} ({op}) falls through into labeled {irseq}");
    }
}

#[test]
fn br_true_false_are_int64() {
    let mut kv = fresh_kv();
    compile(&mut kv, "rwfunc f(X:int64) -> (Y:int64) {\n    if (X > 0) { X -> Y } else { 0 -> Y }\n}\n").unwrap();
    let children = children_of(&mut kv, "/lib/f/");
    let mut saw_br = false;
    for c in &children {
        let Some(row) = parse_row(c) else { continue };
        if sig(&kv.get_one(&format!("/lib/f/[{row},0]"))) != "br" {
            continue;
        }
        let t = kv.get_one(&format!("/lib/f/[{row},-2]"));
        let f = kv.get_one(&format!("/lib/f/[{row},-3]"));
        assert_eq!(kvkind::kind(&t), "int64", "br true {}", kvkind::display(&t));
        assert_eq!(kvkind::kind(&f), "int64", "br false {}", kvkind::display(&f));
        saw_br = true;
    }
    assert!(saw_br, "no br in {children:?}");
}

#[test]
fn abs_if_is_numbered_jumps() {
    let mut kv = compile_fn(
        "rwfunc abs(x:int64) -> (r:int64) {\n    if (x < 0) {\n        0 - x -> r\n    } else {\n        x -> r\n    }\n}\n",
    );
    assert_single_plane(&mut kv, "abs");
    let p = plane(&mut kv, "abs");
    let ir = dump_ir(&p);
    eprintln!("abs IR:\n{ir}");
    assert_jumps_are_int64(&mut kv, "abs", &p);
    assert_no_scope_opcode(&p, "abs");
    let ops: Vec<_> = p.iter().filter(|(n, _)| **n > 0).map(|(_, i)| i.op.as_str()).collect();
    assert!(ops.contains(&"br"), "abs missing br:\n{ir}");
    assert!(ops.contains(&"goto"), "abs missing goto:\n{ir}");
    assert!(ops.contains(&"return"), "abs missing return:\n{ir}");
    assert!(!ops.contains(&"call"), "abs if lowered to call:\n{ir}");
    let labs = labels_of(&mut kv, "abs");
    assert!(labs.keys().any(|k| k.contains("_if_")), "abs labels={labs:?}");
    assert!(labs.keys().any(|k| k.contains("_then_")), "abs labels={labs:?}");
    assert!(labs.keys().any(|k| k.contains("_else_")), "abs labels={labs:?}");
    assert!(labs.keys().any(|k| k.contains("_merge_")), "abs labels={labs:?}");
    for (name, irseq) in &labs {
        let inst = p.get(irseq).unwrap_or_else(|| panic!("label {name} -> {irseq} missing"));
        assert!(!inst.op.is_empty(), "label {name} -> empty op at {irseq}");
    }
}

#[test]
fn while_back_edge_is_goto_int64() {
    let mut kv = compile_fn(
        "rwfunc sum_to(n:int64) -> (acc:int64) {\n    0 -> acc\n    1 -> i\n    while (i <= n) {\n        acc + i -> acc\n        i + 1 -> i\n    }\n}\n",
    );
    assert_single_plane(&mut kv, "sum_to");
    let p = plane(&mut kv, "sum_to");
    let ir = dump_ir(&p);
    eprintln!("sum_to IR:\n{ir}");
    assert_jumps_are_int64(&mut kv, "sum_to", &p);
    assert_no_scope_opcode(&p, "sum_to");
    let labs = labels_of(&mut kv, "sum_to");
    let cond = labs
        .iter()
        .find(|(k, _)| k.contains("_while_"))
        .map(|(_, v)| *v)
        .unwrap_or_else(|| panic!("no _while_ label: {labs:?}"));
    let back = p.iter().any(|(n, inst)| {
        *n > 0 && inst.op == "goto" && inst.reads.first().map(|s| s.parse() == Ok(cond)).unwrap_or(false)
    });
    assert!(back, "while body has no goto {cond}:\n{ir}\nlabels={labs:?}");
    assert!(!p.values().any(|i| i.op == "call"), "while lowered to call:\n{ir}");
}

#[test]
fn break_is_goto_exit() {
    let mut kv = compile_fn(
        "rwfunc f() -> (i:int64) {\n    0 -> i\n    while (i < 10) {\n        if (i == 5) {\n            break\n        }\n        i <- i + 1\n    }\n}\n",
    );
    assert_single_plane(&mut kv, "f");
    let p = plane(&mut kv, "f");
    let ir = dump_ir(&p);
    eprintln!("break IR:\n{ir}");
    assert_jumps_are_int64(&mut kv, "f", &p);
    assert_no_scope_opcode(&p, "f");
    let labs = labels_of(&mut kv, "f");
    let exit = labs
        .iter()
        .find(|(k, _)| k.contains("_exit_"))
        .map(|(_, v)| *v)
        .unwrap_or_else(|| panic!("no _exit_ label: {labs:?}"));
    let to_exit = p.iter().filter(|(_, inst)| {
        inst.op == "goto" && inst.reads.first().map(|s| s.parse() == Ok(exit)).unwrap_or(false)
    }).count();
    assert!(to_exit >= 1, "break did not emit goto {exit}:\n{ir}\nlabels={labs:?}");
}

#[test]
fn continue_is_goto_cond() {
    let mut kv = compile_fn(
        "rwfunc f() -> (acc:int64) {\n    0 -> acc\n    1 -> i\n    while (i <= 5) {\n        if (i == 3) {\n            i <- i + 1\n            continue\n        }\n        acc <- acc + i\n        i <- i + 1\n    }\n}\n",
    );
    assert_single_plane(&mut kv, "f");
    let p = plane(&mut kv, "f");
    let ir = dump_ir(&p);
    eprintln!("continue IR:\n{ir}");
    assert_jumps_are_int64(&mut kv, "f", &p);
    assert_no_scope_opcode(&p, "f");
    let labs = labels_of(&mut kv, "f");
    let cond = labs
        .iter()
        .find(|(k, _)| k.contains("_while_"))
        .map(|(_, v)| *v)
        .unwrap_or_else(|| panic!("no _while_ label: {labs:?}"));
    let to_cond = p.iter().filter(|(_, inst)| {
        inst.op == "goto" && inst.reads.first().map(|s| s.parse() == Ok(cond)).unwrap_or(false)
    }).count();
    assert!(to_cond >= 2, "continue+latch should goto cond {cond} at least twice, got {to_cond}:\n{ir}\nlabels={labs:?}");
}

#[test]
fn for_is_br_goto_single_plane() {
    let mut kv = compile_fn(
        "rwfunc f() -> () {\n    data·0 <- 0\n    data·1 <- 1\n    for (x in data) {\n        x -> _\n    }\n}\n",
    );
    assert_single_plane(&mut kv, "f");
    let p = plane(&mut kv, "f");
    let ir = dump_ir(&p);
    eprintln!("for IR:\n{ir}");
    assert_jumps_are_int64(&mut kv, "f", &p);
    assert_no_scope_opcode(&p, "f");
    let labs = labels_of(&mut kv, "f");
    assert!(labs.keys().any(|k| k.contains("_for_")), "for labels={labs:?}");
    assert!(p.values().any(|i| i.op == "br"), "for missing br:\n{ir}");
    assert!(!p.values().any(|i| i.op == "call" && i.reads.first().map(|s| s.contains("_for_")).unwrap_or(false)), "for still calls a scope:\n{ir}");
}

#[test]
fn nested_if_while_single_plane() {
    let mut kv = compile_fn(
        "rwfunc f() -> (acc:int64) {\n    0 -> acc\n    1 -> i\n    while (i <= 3) {\n        if (i == 2) {\n            acc <- acc + i\n        } else {\n            acc <- acc + 1\n        }\n        i <- i + 1\n    }\n}\n",
    );
    assert_single_plane(&mut kv, "f");
    let p = plane(&mut kv, "f");
    assert_jumps_are_int64(&mut kv, "f", &p);
    assert_no_scope_opcode(&p, "f");
    let brs = p.values().filter(|i| i.op == "br").count();
    assert!(brs >= 2, "nested if+while should have >=2 br, got {brs}:\n{}", dump_ir(&p));
}

#[test]
fn tutorial_control_files_single_plane() {
    for (file, fns) in [
        ("../tutorial/03-control/if.kv", &["my·abs"][..]),
        ("../tutorial/03-control/while.kv", &["sum_to", "first_div7", "sum_odds"][..]),
        ("../tutorial/03-control/guess.kv", &["guess_number"][..]),
        ("../tutorial/03-control/classify.kv", &["classify"][..]),
        ("../tutorial/03-control/for.kv", &["test"][..]),
        ("../tutorial/06-algo/prime_sieve.kv", &["prime_sieve"][..]),
    ] {
        let mut kv = fresh_kv();
        let src = std::fs::read_to_string(file).unwrap();
        compile(&mut kv, &src).unwrap();
        for fn_ in fns {
            assert_single_plane(&mut kv, fn_);
            let p = plane(&mut kv, fn_);
            assert_jumps_are_int64(&mut kv, fn_, &p);
            assert_no_scope_opcode(&p, fn_);
        }
    }
}
