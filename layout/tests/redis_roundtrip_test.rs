use kvlang_layout::{compile, init_dirs, kvkind, Kv};

fn body(data: &[u8]) -> &[u8] {
    let h = kvkind::head(data);
    kvkind::body(data, &h)
}

/// 需要 redis 运行在 127.0.0.1:6379。
#[test]
fn redis_roundtrip() {
    let mut kv = Kv::conn("redis://127.0.0.1:6379");
    init_dirs(&mut kv).unwrap();

    // 写一个 rwir（Opaque kind）并读回（无 `.` 的 plain key，避免 dict 拆分）
    let v = kvkind::new_rwir(0, 0, "testop");
    kv.set(&[("/lib/testop".to_string(), v)]).unwrap();
    let got = kv.get_one("/lib/testop");
    assert_eq!(kvkind::kind(&got), "rwir");
    assert_eq!(String::from_utf8_lossy(&body(&got)[4..]), "testop");

    // 目录索引可见
    let children = kv.list("/lib/", false, false);
    assert!(children.iter().any(|c| c.contains("testop")));

    kv.del_tree("/lib/testop").unwrap();
    assert!(kv.get_one("/lib/testop").is_empty());
}

#[test]
fn redis_compile() {
    let mut kv = Kv::conn("redis://127.0.0.1:6379");
    init_dirs(&mut kv).unwrap();

    let src = "rwfunc sum(A:int64, B:int64) -> (C:int64) {\n    A + B -> C\n}\n";
    compile(&mut kv, src).unwrap();

    let sig_val = kv.get_one("/lib/sum/[0,0]");
    assert_eq!(kvkind::kind(&sig_val), "defrwfunc");
    let b = body(&sig_val);
    assert_eq!(kvkind::rwfunc_num_reads(b), 2);
    assert_eq!(kvkind::rwfunc_num_writes(b), 1);

    let op = kv.get_one("/lib/sum/[1,0]");
    assert_eq!(kvkind::kind(&op), "rwir|rwfunc");
    assert_eq!(String::from_utf8_lossy(&body(&op)[4..]), "int64.add");
}
