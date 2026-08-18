use kvlang_layout::{compile, init_dirs, kvkind, Kv};

fn body(data: &[u8]) -> &[u8] {
    let h = kvkind::head(data);
    kvkind::body(data, &h)
}

fn sig(data: &[u8]) -> String {
    let b = body(data);
    String::from_utf8_lossy(&b[4.min(b.len())..]).into_owned()
}

fn fresh_kv() -> Kv {
    let dsn = format!("fs:///tmp/kvlang_layout_test_{}", std::process::id());
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
    assert_eq!(kvkind::kind(&sig_val), "rwfunc");
    assert_eq!(kvkind::array_len(&sig_val), 1);
    let b = body(&sig_val);
    assert_eq!(kvkind::rwfunc_num_reads(b), 2);
    assert_eq!(kvkind::rwfunc_num_writes(b), 1);
    // kindexp 列表：读参在前(nr=2)、写参在后(nw=1)
    assert_eq!(kvkind::rwfunc_param_types(b), vec!["int64", "int64", "int64"]);

    // 参数 Ptr
    let a = kv.get_one("/lib/sum/A");
    assert!(kvkind::is_ptr(&a));
    assert_eq!(kvkind::ptr_target(&a), "[0,-1]");
    let c = kv.get_one("/lib/sum/C");
    assert!(kvkind::is_ptr(&c));
    assert_eq!(kvkind::ptr_target(&c), "[0,1]");

    // 指令（特化后 opcode = int64.add）
    let op = kv.get_one("/lib/sum/[1,0]");
    assert_eq!(kvkind::kind(&op), "rwir");
    assert_eq!(sig(&op), "int64.add");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,-1]")), "A");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,-2]")), "B");
    assert_eq!(sig(&kv.get_one("/lib/sum/[1,1]")), "C");

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
    assert_eq!(kvkind::kind(&sig_val), "rwfunc");

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
fn compile_control_flow_lowers_to_blocks() {
    let mut kv = fresh_kv();
    let src = "rwfunc f(X:int64) -> (Y:int64) {\n    if (X > 0) {\n        X -> Y\n    } else {\n        0 -> Y\n    }\n}\n";
    compile(&mut kv, src).unwrap();

    let sig_val = kv.get_one("/lib/f/[0,0]");
    assert_eq!(kvkind::kind(&sig_val), "rwfunc");
    // 降级后存在 if/then 基本块（label 指令以 _label[coord] 扁平键存在；空 merge 块不落盘）
    let children = kv.list("/lib/f/", false, false);
    assert!(children.iter().any(|c| c.contains("_if_")));
    assert!(children.iter().any(|c| c.contains("_then_")));
}
