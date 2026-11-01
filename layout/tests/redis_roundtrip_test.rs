use kvlanglayout::{compile, init_dirs, kvkind, Kv};

fn body(data: &[u8]) -> &[u8] {
    let h = kvkind::head(data);
    kvkind::body(data, &h)
}

/// Run only when `KVLANG_TEST_REDIS_DSN` is explicitly set to avoid polluting a local Redis instance.
fn redis_dsn() -> Option<String> {
    let dsn = std::env::var("KVLANG_TEST_REDIS_DSN").ok().filter(|d| !d.is_empty());
    if dsn.is_none() {
        eprintln!("skip: set KVLANG_TEST_REDIS_DSN to a dedicated redis to run this test");
    }
    dsn
}

/// Use a dedicated Redis instance for this test.
#[test]
fn redis_roundtrip() {
    let Some(dsn) = redis_dsn() else { return };
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();

    // 写一个 rwir（Opaque kind）并读回（无 `.` 的 plain key，避免 dict 拆分）
    let v = kvkind::new_rwir(0, 0, "testop").unwrap();
    kv.set(&[("/lib/testop".to_string(), "rwir".to_string(), v)]).unwrap();
    let got = kv.get_one("/lib/testop");
    assert_eq!(kvkind::kind(&got, &kv.langtype_of("/lib/testop")), "rwir");
    assert_eq!(String::from_utf8_lossy(&body(&got)[5..]), "testop");

    // 目录索引可见
    let children = kv.list("/lib/", false, false);
    assert!(children.iter().any(|c| c.contains("testop")));

    kv.del_tree("/lib/testop").unwrap();
    assert!(kv.get_one("/lib/testop").is_empty());
}

#[test]
fn redis_compile() {
    let Some(dsn) = redis_dsn() else { return };
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();

    let src = "rwfunc sum(A:int64, B:int64) -> (C:int64) {\n    A + B -> C\n}\n";
    compile(&mut kv, src).unwrap();

    let sig_val = kv.get_one("/lib/sum/[0,0]");
    assert_eq!(kvkind::kind(&sig_val, &kv.langtype_of("/lib/sum/[0,0]")), "rwfunc");
    let b = body(&sig_val);
    assert_eq!(kvkind::rwfunc_num_reads(b), 2);
    assert_eq!(kvkind::rwfunc_num_writes(b), 1);

    let op = kv.get_one("/lib/sum/[1,0]");
    assert_eq!(kvkind::kind(&op, &kv.langtype_of("/lib/sum/[1,0]")), "rwir");
    assert_eq!(String::from_utf8_lossy(&body(&op)[5..]), "int64·add");
}
