
use kvlanglayout::{compile, init_dirs, Kv};
use std::path::PathBuf;
use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_kvlanglayout")
}

fn temp_file(tag: &str, src: &str) -> PathBuf {
    let path = std::env::temp_dir().join(format!(
        "kvlanglayout_unionmsg_{}_{}.kv",
        tag,
        std::process::id()
    ));
    
    let _ = std::fs::remove_file(&path);
    std::fs::write(&path, src).unwrap();
    path
}

fn dsn(tag: &str) -> String {
    kvlanglayout::ffi::test_dsn(&format!("unionmsg_{tag}"))
}

struct TempStore(String);

impl Drop for TempStore {
    fn drop(&mut self) {
        kvlanglayout::ffi::remove_test_store(&self.0);
    }
}

fn fresh_kv(tag: &str) -> (TempStore, Kv) {
    let dsn = kvlanglayout::ffi::test_dsn(&format!("union_{tag}"));
    let store = TempStore(dsn.clone());
    kvlanglayout::ffi::remove_test_store(&dsn);

    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    (store, kv)
}

#[test]
fn union_members_accepted() {
    let (_store, mut kv) = fresh_kv("ok");
    let decl = "rwir f(*int64|float64 #x) -> (*int64 #r)\n";
    compile(&mut kv, decl).unwrap();
    compile(
        &mut kv,
        &format!("{decl}rwfunc g() -> () {{\n    f(1)\n}}\n"),
    )
    .unwrap();
    compile(
        &mut kv,
        &format!("{decl}rwfunc h() -> () {{\n    f(1.5)\n}}\n"),
    )
    .unwrap();
    compile(
        &mut kv,
        &format!("{decl}rwfunc i(*int64 #v) -> () {{\n    f(*v)\n}}\n"),
    )
    .unwrap();
}

#[test]
fn union_non_members_rejected() {
    let (_store, mut kv) = fresh_kv("bad");
    let decl = "rwir f(*int64|float64 #x) -> (*int64 #r)\n";
    compile(&mut kv, decl).unwrap();
    
    assert!(compile(
        &mut kv,
        &format!("{decl}rwfunc g() -> () {{\n    f(true)\n}}\n")
    )
    .is_err());
    
    assert!(compile(
        &mut kv,
        &format!("{decl}rwfunc h() -> () {{\n    a:int32 = 1\n    f(a)\n}}\n")
    )
    .is_err());
    
    assert!(compile(
        &mut kv,
        &format!("{decl}rwfunc i() -> () {{\n    f(\"s\")\n}}\n")
    )
    .is_err());
}

#[test]
fn union_pointer_and_string_members() {
    let (_store, mut kv) = fresh_kv("ptr");
    compile(
        &mut kv,
        "struct P {\n\tx:int64 = 0\n}\nrwir f(*P|int64 #p) -> () \n",
    )
    .unwrap();
    compile(&mut kv, "rwfunc g() -> () {\n    1\n    f(1)\n}\n").unwrap();
    compile(&mut kv, "rwir f2(*[]char/utf8|int64 #s) -> ()\n").unwrap();
    compile(&mut kv, "rwfunc h() -> () {\n    f2(\"x\")\n}\n").unwrap();
}

#[test]
fn union_mismatch_reports_both_sides() {
    let file = temp_file(
        "bad",
        "rwir f(*int64|float64 #x) -> (*int64 #r)\nrwfunc g() -> () {\n    f(true)\n}\n",
    );
    let db = dsn("bad");
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(!out.status.success());
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(
        err.contains("cannot pass bool to int64|float64 (parameter x)"),
        "stderr: {err}"
    );
    let _ = std::fs::remove_file(&file);
    kvlanglayout::ffi::remove_test_store(&db);
}
