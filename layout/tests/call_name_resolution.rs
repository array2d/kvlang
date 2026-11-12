
use kvlanglayout::{compile, init_dirs, Kv};

struct TempStore(String);

impl Drop for TempStore {
    fn drop(&mut self) {
        kvlanglayout::ffi::remove_test_store(&self.0);
    }
}

fn fresh_kv(tag: &str) -> (TempStore, Kv) {
    let dsn = kvlanglayout::ffi::test_dsn(&format!("callname_{tag}"));
    let store = TempStore(dsn.clone());
    kvlanglayout::ffi::remove_test_store(&dsn);

    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    (store, kv)
}

#[test]
fn same_package_bare_call_is_rejected() {
    let (_store, mut kv) = fresh_kv("bare");
    let err = compile(
        &mut kv,
        "lib p {\n    rwfunc helper_int(*int64 #x) -> (*int64 #r) { *x -> *r }\n    rwfunc caller() -> (*int64 #r) { helper_int(1) -> *r }\n}\n",
    )
    .unwrap_err();
    assert!(err.contains("未定义函数 helper_int"), "{err}");
    assert!(err.contains("未给出被调函数的完整路径"), "{err}");
}

#[test]
fn same_package_qualified_call_is_accepted() {
    let (_store, mut kv) = fresh_kv("qual");
    compile(
        &mut kv,
        "lib p {\n    rwfunc helper_int(*int64 #x) -> (*int64 #r) { *x -> *r }\n    rwfunc caller() -> (*int64 #r) { p·helper_int(1) -> *r }\n}\n",
    )
    .unwrap();
}

#[test]
fn global_bare_call_is_accepted() {
    let (_store, mut kv) = fresh_kv("global");
    compile(
        &mut kv,
        "rwfunc helper_int(*int64 #x) -> (*int64 #r) { *x -> *r }\nrwfunc caller() -> (*int64 #r) { helper_int(1) -> *r }\n",
    )
    .unwrap();
}

#[test]
fn absolute_lib_path_is_accepted() {
    let (_store, mut kv) = fresh_kv("abs");
    compile(
        &mut kv,
        "lib p {\n    rwfunc helper_int(*int64 #x) -> (*int64 #r) { *x -> *r }\n    rwfunc caller() -> (*int64 #r) { /lib/p·helper_int(1) -> *r }\n}\n",
    )
    .unwrap();
}

#[test]
fn absolute_global_path_is_accepted() {
    let (_store, mut kv) = fresh_kv("absg");
    compile(
        &mut kv,
        "rwfunc helper_int(*int64 #x) -> (*int64 #r) { *x -> *r }\nrwfunc caller() -> (*int64 #r) { /lib/helper_int(1) -> *r }\n",
    )
    .unwrap();
}

#[test]
fn global_wins_over_same_package_function() {
    
    let (_store, mut kv) = fresh_kv("shadow");
    compile(
        &mut kv,
        "rwfunc f(*int64 #x) -> (*int64 #r) { *x -> *r }\nlib p {\n    rwfunc f(*int64 #x) -> (*int64 #r) { *x -> *r }\n    rwfunc caller() -> (*int64 #r) { f(1) -> *r }\n}\n",
    )
    .unwrap();
}

#[test]
fn unknown_bare_call_is_rejected() {
    
    let (_store, mut kv) = fresh_kv("unknown");
    let err = compile(&mut kv, "rwfunc t() -> () {\n    debugger()\n}\n").unwrap_err();
    assert!(err.contains("未定义函数 debugger"), "{err}");
    assert!(err.contains("未给出被调函数的完整路径"), "{err}");
}
