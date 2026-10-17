use std::sync::atomic::{AtomicU64, Ordering};

use kvlang_layout::{compile, init_dirs, kvkind, Kv};

static KV_SEQ: AtomicU64 = AtomicU64::new(0);

fn body(data: &[u8]) -> &[u8] {
    let h = kvkind::head(data);
    kvkind::body(data, &h)
}

fn sig(data: &[u8]) -> String {
    let b = body(data);
    String::from_utf8_lossy(&b[4.min(b.len())..]).into_owned()
}

fn frame_param(kv: &mut Kv, func_dir: &str, name: &str) -> Vec<u8> {
    let frame = format!("/vthread/ptrcheck/{name}/[1]/");
    kv.mkindex(&frame).unwrap();
    kv.ext_index(&frame, func_dir).unwrap();
    kv.get_one(&format!("/vthread/ptrcheck/{name}/[1]/{name}"))
}

fn fresh_kv() -> Kv {
    let dsn = format!(
        "shm:///tmp/kvlang_layout_test_{}_{}",
        std::process::id(),
        KV_SEQ.fetch_add(1, Ordering::Relaxed)
    );
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    kv
}

#[test]
fn refuse_rwir_and_rwfunc() {
    let mut kv = fresh_kv();
    compile(&mut kv, "rwfunc dup() -> () {\n    1 -> _\n}\n").unwrap();
    let err = compile(&mut kv, "rwir dup() -> ()\n").unwrap_err();
    assert!(err.contains("one opcode, one definition"), "{err}");
}

#[test]
fn compile_simple_func() {
    let mut kv = fresh_kv();
    let src = "rwfunc sum(A:int64, B:int64) -> (C:int64) {\n    A + B -> C\n}\n";
    compile(&mut kv, src).unwrap();

    let sig_val = kv.get_one("/lib/sum/[0,0]");
    assert_eq!(kvkind::kind(&sig_val), "defrwfunc");
    let b = body(&sig_val);
    assert_eq!(kvkind::rwfunc_num_reads(b), 2);
    assert_eq!(kvkind::rwfunc_num_writes(b), 1);
    // kindexp 列表：读参在前(nr=2)、写参在后(nw=1)
    assert_eq!(kvkind::rwfunc_param_types(b), vec!["int64", "int64", "int64"]);

    // Get follows ref=1. The slot [0,±j] is not a key yet, so a direct read is empty.
    // Runtime sees the raw Ptr through the frame extindex. Same path here.
    let a = frame_param(&mut kv, "/lib/sum/", "A");
    assert!(kvkind::is_ptr(&a), "A len={} kind={}", a.len(), kvkind::kind(&a));
    assert_eq!(kvkind::ptr_target(&a), "[0,-1]");
    let c = frame_param(&mut kv, "/lib/sum/", "C");
    assert!(kvkind::is_ptr(&c));
    assert_eq!(kvkind::ptr_target(&c), "[0,1]");

    // 指令（特化后 opcode = int64.add）
    let op = kv.get_one("/lib/sum/[1,0]");
    assert_eq!(kvkind::kind(&op), "rwir|rwfunc");
    assert_eq!(sig(&op), "int64.add");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,-1]")), "A");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,-2]")), "B");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,1]")), "C");
    assert_eq!(kvkind::array_len(&sig_val), 2);
    assert_eq!(sig(&kv.get_one("/lib/sum/[2,0]")), "return");

    // 源码副本
    let src_val = kv.get_one("/lib/sum.src");
    assert_eq!(kvkind::kind(&src_val), "char/utf8");
}

#[test]
fn compile_string_literal_and_lib() {
    let mut kv = fresh_kv();
    let src = "lib p { rwfunc hi() -> () {\n    \"hello\" -> _\n} }\n";
    compile(&mut kv, src).unwrap();

    // 函数在 /lib/p.hi/ 下（lib 块 pkg 前缀）
    let sig_val = kv.get_one("/lib/p.hi/[0,0]");
    assert_eq!(kvkind::kind(&sig_val), "defrwfunc");

    // 字符串字面量读槽 → char/utf32（UTF-32 LE 码点）
    let r = kv.get_one("/lib/p.hi/[1,-1]");
    assert_eq!(kvkind::kind(&r), "char/utf32");
    let b = body(&r);
    let s: String = b
        .chunks_exact(4)
        .map(|c| char::from_u32(u32::from_le_bytes([c[0], c[1], c[2], c[3]])).unwrap_or('\u{FFFD}'))
        .collect();
    assert_eq!(s, "hello");
}

#[test]
fn compile_multiline_string_literal() {
    let mut kv = fresh_kv();
    let src = "lib m { rwfunc ml() -> () {\n    \"\"\"hello\nworld\"\"\" -> _\n} }\n";
    compile(&mut kv, src).unwrap();

    let r = kv.get_one("/lib/m.ml/[1,-1]");
    assert_eq!(kvkind::kind(&r), "char/utf32");
    let b = body(&r);
    let s: String = b
        .chunks_exact(4)
        .map(|c| char::from_u32(u32::from_le_bytes([c[0], c[1], c[2], c[3]])).unwrap_or('\u{FFFD}'))
        .collect();
    assert_eq!(s, "hello\nworld");
}

#[test]
fn compile_control_flow_lowers_to_blocks() {
    let mut kv = fresh_kv();
    let src = "rwfunc f(X:int64) -> (Y:int64) {\n    if (X > 0) {\n        X -> Y\n    } else {\n        0 -> Y\n    }\n}\n";
    compile(&mut kv, src).unwrap();

    let sig_val = kv.get_one("/lib/f/[0,0]");
    assert_eq!(kvkind::kind(&sig_val), "defrwfunc");
    let children = kv.list("/lib/f/", false, false);
    assert!(
        children.iter().all(|c| !c.contains("_if_") && !c.contains("_then_") && !c.contains("_else_") && !c.contains("_merge_")),
        "scope names must not appear in /lib/f/ keys: {children:?}"
    );
    assert_eq!(sig(&kv.get_one("/lib/f/[1,0]")), "goto");
    let tgt = sig(&kv.get_one("/lib/f/[1,-1]"));
    assert!(tgt.starts_with('[') && tgt.ends_with(",0]"), "goto target must be [n,0], got {tgt}");
    let labels = kv.list("/lib/f/\u{2025}labels/", false, false);
    assert!(labels.iter().any(|c| c.contains("_if_")), "‥labels must keep lower names: {labels:?}");
}

fn opcode_at(kv: &mut Kv, func: &str, i: i32) -> String {
    sig(&kv.get_one(&format!("/lib/{func}/[{i},0]")))
}

fn assert_dense_and_jumps(kv: &mut Kv, func: &str) {
    let sig_val = kv.get_one(&format!("/lib/{func}/[0,0]"));
    let n = kvkind::array_len(&sig_val);
    assert!(n >= 1, "{func} empty");
    for i in 1..=n {
        let op = kv.get_one(&format!("/lib/{func}/[{i},0]"));
        assert!(!kvkind::is_none(&op), "{func}: hole at [{i},0]");
    }
    let hole = kv.get_one(&format!("/lib/{func}/[{},0]", n + 1));
    assert!(kvkind::is_none(&hole), "{func}: extra inst after N");
    for i in 1..=n {
        let op = opcode_at(kv, func, i);
        if op == "goto" {
            let t = sig(&kv.get_one(&format!("/lib/{func}/[{i},-1]")));
            assert!(t.starts_with('[') && t.ends_with(",0]"), "{func}[{i}] goto {t}");
            let dest: i32 = t[1..t.len() - 3].parse().unwrap();
            assert!(dest >= 1 && dest <= n, "{func}[{i}] goto {t} out of 1..={n}");
        }
        if op == "br" {
            for j in 2..=3 {
                let t = sig(&kv.get_one(&format!("/lib/{func}/[{i},-{j}]")));
                assert!(t.starts_with('[') && t.ends_with(",0]"), "{func}[{i}] br {t}");
                let dest: i32 = t[1..t.len() - 3].parse().unwrap();
                assert!(dest >= 1 && dest <= n, "{func}[{i}] br {t} out of 1..={n}");
            }
        }
    }
}

#[test]
fn compile_nested_if_while_is_dense() {
    let mut kv = fresh_kv();
    compile(
        &mut kv,
        "rwfunc f(A:int64, B:int64) -> (X:int64) {\n    if (A > 0) {\n        if (B > 0) {\n            1 -> X\n        }\n    }\n    2 -> X\n}\n",
    )
    .unwrap();
    assert_dense_and_jumps(&mut kv, "f");
    let children = kv.list("/lib/f/", false, false);
    assert!(children.iter().all(|c| !c.contains("_if_")), "{children:?}");

    compile(
        &mut kv,
        "rwfunc g(N:int64, hit:bool) -> (Y:int64) {\n    N -> C\n    0 -> Y\n    while (C > 0) {\n        if (hit) {\n            1 -> Y\n        }\n        C - 1 -> C\n    }\n}\n",
    )
    .unwrap();
    assert_dense_and_jumps(&mut kv, "g");
}

#[test]
fn compile_tco_depth_has_no_scope_keys() {
    let src = std::fs::read_to_string("../tutorial/02-func/tco_depth.kv").unwrap();
    let mut kv = fresh_kv();
    compile(&mut kv, &src).unwrap();
    for func in ["tailrec_fact", "sum_while", "main"] {
        let children = kv.list(&format!("/lib/{func}/"), false, false);
        assert!(
            children.iter().all(|c| !c.contains("_else_") && !c.contains("_if_") && !c.contains("_while_")),
            "{func}: {children:?}"
        );
        let ghost = kv.get_one(&format!("/lib/{func}/_else_4[3,0]"));
        assert!(kvkind::is_none(&ghost), "{func} leftover scope key");
        assert_dense_and_jumps(&mut kv, func);
    }
}
