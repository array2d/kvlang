use kvlanglayout::{compile, init_dirs, kvkind, Kv};

fn body(data: &[u8]) -> &[u8] {
    let h = kvkind::head(data);
    kvkind::body(data, &h)
}

fn sig(data: &[u8]) -> String {
    let b = body(data);
    String::from_utf8_lossy(&b[4.min(b.len())..]).into_owned()
}

fn fresh_kv() -> Kv {
    let dsn = format!("fs:///tmp/kvlanglayout_test_{}", std::process::id());
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    kv
}

#[test]
fn compile_simple_func() {
    let mut kv = fresh_kv();
    let src = "rwfunc sum(A:int64, B:int64) -> (C:int64) {\n    A + B -> C\n}\n";
    compile(&mut kv, src).unwrap();

    // 函数签名
    let sig_val = kv.get_one("/lib/sum/[0,0]");
    assert_eq!(kvkind::kind(&sig_val), "defrwfunc");
    assert_eq!(kvkind::array_len(&sig_val), 2); // add + return
    let b = body(&sig_val);
    assert_eq!(kvkind::rwfunc_num_reads(b), 2);
    assert_eq!(kvkind::rwfunc_num_writes(b), 1);
    // kindexp 列表：读参在前(nr=2)、写参在后(nw=1)
    assert_eq!(
        kvkind::rwfunc_param_types(b),
        vec!["int64", "int64", "int64"]
    );

    // 参数 Ptr
    let a = kv.get_one("/lib/sum/A");
    assert!(kvkind::is_ptr(&a));
    assert_eq!(kvkind::ptr_target(&a), "[0,-1]");
    let c = kv.get_one("/lib/sum/C");
    assert!(kvkind::is_ptr(&c));
    assert_eq!(kvkind::ptr_target(&c), "[0,1]");

    // 指令（特化后 opcode = int64·add）
    let op = kv.get_one("/lib/sum/[1,0]");
    assert_eq!(kvkind::kind(&op), "rwir|rwfunc");
    assert_eq!(sig(&op), "int64·add");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,-1]")), "A");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,-2]")), "B");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,1]")), "C");
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

    // 函数在 /lib/p·hi/ 下（lib 块 pkg 前缀）
    let sig_val = kv.get_one("/lib/p·hi/[0,0]");
    assert_eq!(kvkind::kind(&sig_val), "defrwfunc");

    // 字符串字面量读槽 → char/utf32（UTF-32 LE 码点）
    let r = kv.get_one("/lib/p·hi/[1,-1]");
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

    let r = kv.get_one("/lib/m·ml/[1,-1]");
    assert_eq!(kvkind::kind(&r), "char/utf32");
    let b = body(&r);
    let s: String = b
        .chunks_exact(4)
        .map(|c| char::from_u32(u32::from_le_bytes([c[0], c[1], c[2], c[3]])).unwrap_or('\u{FFFD}'))
        .collect();
    assert_eq!(s, "hello\nworld");
}

#[test]
fn compile_control_flow_is_single_plane() {
    let mut kv = fresh_kv();
    let src = "rwfunc f(X:int64) -> (Y:int64) {\n    if (X > 0) {\n        X -> Y\n    } else {\n        0 -> Y\n    }\n}\n";
    compile(&mut kv, src).unwrap();

    let sig_val = kv.get_one("/lib/f/[0,0]");
    assert_eq!(kvkind::kind(&sig_val), "defrwfunc");
    let children = kv.list("/lib/f/", false, false);
    let old_scope = children.iter().any(|c| {
        let b = c.trim_end_matches('/');
        b.contains("_if_") || b.contains("_then_") || b.contains("_else_") || b.contains("_merge_")
    });
    assert!(!old_scope, "scope keys still present: {children:?}");
    assert!(children.iter().any(|c| c.contains("labels")), "missing ‥labels: {children:?}");

    let labels = kv.list("/lib/f/\u{2025}labels/", false, false);
    let if_name = labels.iter().map(|c| c.trim_end_matches('/')).find(|c| c.contains("_if_"));
    assert!(if_name.is_some(), "‥labels={labels:?}");
    let if_irseq = kv.get_one(&format!("/lib/f/\u{2025}labels/{}", if_name.unwrap()));
    assert_eq!(kvkind::kind(&if_irseq), "int64", "label kind={} labels={labels:?}", kvkind::display(&if_irseq));

    // irseq 1 is preamble goto; target is int64
    let goto_op = kv.get_one("/lib/f/[1,0]");
    assert_eq!(sig(&goto_op), "goto");
    let tgt = kv.get_one("/lib/f/[1,-1]");
    assert_eq!(kvkind::kind(&tgt), "int64", "goto target {}", kvkind::display(&tgt));
}

#[test]
fn compile_while_goto_targets_are_int64() {
    let mut kv = fresh_kv();
    let src = "rwfunc sum_to(n:int64) -> (acc:int64) {\n    0 -> acc\n    1 -> i\n    while (i <= n) {\n        acc + i -> acc\n        i + 1 -> i\n    }\n}\n";
    compile(&mut kv, src).unwrap();
    let children = kv.list("/lib/sum_to/", false, false);
    assert!(
        !children.iter().any(|c| c.contains("_while_") || c.contains("_do_")),
        "while still has scope keys: {children:?}"
    );
    let mut saw_int_goto = false;
    for c in &children {
        let t = c.trim_end_matches('/');
        if !t.starts_with('[') || !t.ends_with(",0]") {
            continue;
        }
        if sig(&kv.get_one(&format!("/lib/sum_to/{t}"))) != "goto" {
            continue;
        }
        let row: i32 = t.trim_start_matches('[').split(',').next().unwrap().parse().unwrap();
        let tgt = kv.get_one(&format!("/lib/sum_to/[{row},-1]"));
        assert_eq!(kvkind::kind(&tgt), "int64");
        saw_int_goto = true;
    }
    assert!(saw_int_goto, "no goto found in {children:?}");
}
