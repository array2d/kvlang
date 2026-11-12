
use kvlanglayout::code::{compile, init_dirs};
use kvlanglayout::kvkind;
use kvlanglayout::{ffi::Kv, keytree};

struct TempStore(String);
impl Drop for TempStore {
    fn drop(&mut self) {
        
        let base = self.0.trim_start_matches("shm://");
        let _ = std::fs::remove_file(base);
        for suffix in [".head", ".data", ".gen", ".garb"] {
            let _ = std::fs::remove_file(format!("{base}{suffix}"));
        }
    }
}

fn fresh_kv() -> (TempStore, Kv) {
    
    static NEXT: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
    let ordinal = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let dsn = format!(
        "shm:///tmp/kvlang_output_opid_{}_{}.shm",
        std::process::id(),
        ordinal
    );
    let store = TempStore(dsn.clone());
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    (store, kv)
}

fn ops_of(kv: &mut Kv, func: &str) -> Vec<(i32, String)> {
    let prefix = format!("/lib/{func}/");
    let mut out = Vec::new();
    for child in kv.list(&prefix, false, false) {
        let name = child.trim_end_matches('/');
        
        let Some(inner) = name.strip_prefix('[').and_then(|x| x.strip_suffix(",0]")) else {
            continue;
        };
        let Ok(irseq) = inner.parse::<i32>() else {
            continue;
        };
        if irseq < 1 {
            continue;
        }
        let key = format!("{prefix}{name}");
        let wire = kv.get_one(&key);
        out.push((irseq, kvkind::display(&wire, &kv.langtype_of(&key))));
    }
    out.sort_by_key(|(i, _)| *i);
    out
}

#[test]
fn output_ops_become_decimal_opid() {
    let (_store, mut kv) = fresh_kv();
    
    let src = "\
rwir println(*[]char/utf32 #s...)
rwir print(*[]char/utf32 #s...)
rwir cerr(*[]char/utf32 #s...)
rwfunc main() -> () {
    println(\"a\")
    print(\"b\")
    cerr(\"c\")
}
";
    compile(&mut kv, src).unwrap();

    let ops: Vec<String> = ops_of(&mut kv, "main")
        .into_iter()
        .map(|(_, o)| o.rsplit(' ').next().unwrap_or("").to_string())
        .collect();
    
    assert_eq!(
        &ops[..3],
        ["65281", "65282", "65283"],
        "三条输出算子应依次编成 0xFF01/02/03 的十进制，实际 {ops:?}"
    );
}

#[test]
fn ordinary_ops_keep_their_names() {
    let (_store, mut kv) = fresh_kv();
    
    let src = "rwfunc main(*int64 #a, *int64 #b) -> (*int64 #c) {\n    *a + *b -> *c\n}\n";
    compile(&mut kv, src).unwrap();
    let ops: Vec<String> = ops_of(&mut kv, "main")
        .into_iter()
        .map(|(_, o)| o.rsplit(' ').next().unwrap_or("").to_string())
        .collect();
    assert!(
        ops.contains(&"add".to_string()),
        "普通算子应保留名字，实际 {ops:?}"
    );
}

#[test]
fn package_prefixed_output_name_is_not_rewritten() {
    let (_store, mut kv) = fresh_kv();
    
    let src = "\
rwir x·println(*[]char/utf32 #s...)
rwfunc main() -> () {
    x·println(\"a\")
}
";
    compile(&mut kv, src).unwrap();
    let ops: Vec<String> = ops_of(&mut kv, "main")
        .into_iter()
        .map(|(_, o)| o.rsplit(' ').next().unwrap_or("").to_string())
        .collect();
    assert!(
        ops.contains(&"x·println".to_string()),
        "带包前缀的名字不该被改写，实际 {ops:?}"
    );
}

#[test]
fn opid_table_matches_reserved_region() {
    
    for name in ["println", "print", "cerr"] {
        let id = kvlanglayout::builtin::output_opid(name).unwrap();
        assert!(
            id >= 0xFF00,
            "{name} 的 opid {id:#06x} 不在保留区 0xFF00–0xFFFF 内"
        );
    }
    
    let _ = keytree::MEMBER_SEP;
}
