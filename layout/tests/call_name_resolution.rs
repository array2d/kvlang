//! 调用名解析：调用名必须是「全路径去掉 /lib」——全局写 `name`，包内写 `包名·name`。
//! 包内裸名不回退到同包函数；与同包函数同名时 layout 报错。

use kvlanglayout::{compile, init_dirs, Kv};

fn fresh_kv(tag: &str) -> Kv {
    let dsn = format!(
        "fs:///tmp/kvlanglayout_callname_{}_{}_{}",
        tag,
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

#[test]
fn same_package_bare_call_is_rejected() {
    let mut kv = fresh_kv("bare");
    let err = compile(
        &mut kv,
        "lib p {\n    rwfunc helper_int(*int64 $x) -> (*int64 $r) { *x -> *r }\n    rwfunc caller() -> (*int64 $r) { helper_int(1) -> *r }\n}\n",
    )
    .unwrap_err();
    assert!(err.contains("未定义函数 helper_int"), "{err}");
    assert!(err.contains("未给出被调函数的完整路径"), "{err}");
}

#[test]
fn same_package_qualified_call_is_accepted() {
    let mut kv = fresh_kv("qual");
    compile(
        &mut kv,
        "lib p {\n    rwfunc helper_int(*int64 $x) -> (*int64 $r) { *x -> *r }\n    rwfunc caller() -> (*int64 $r) { p·helper_int(1) -> *r }\n}\n",
    )
    .unwrap();
}

#[test]
fn global_bare_call_is_accepted() {
    let mut kv = fresh_kv("global");
    compile(
        &mut kv,
        "rwfunc helper_int(*int64 $x) -> (*int64 $r) { *x -> *r }\nrwfunc caller() -> (*int64 $r) { helper_int(1) -> *r }\n",
    )
    .unwrap();
}

#[test]
fn absolute_lib_path_is_accepted() {
    let mut kv = fresh_kv("abs");
    compile(
        &mut kv,
        "lib p {\n    rwfunc helper_int(*int64 $x) -> (*int64 $r) { *x -> *r }\n    rwfunc caller() -> (*int64 $r) { /lib/p·helper_int(1) -> *r }\n}\n",
    )
    .unwrap();
}

#[test]
fn absolute_global_path_is_accepted() {
    let mut kv = fresh_kv("absg");
    compile(
        &mut kv,
        "rwfunc helper_int(*int64 $x) -> (*int64 $r) { *x -> *r }\nrwfunc caller() -> (*int64 $r) { /lib/helper_int(1) -> *r }\n",
    )
    .unwrap();
}

#[test]
fn global_wins_over_same_package_function() {
    // 裸名只指向全局 /lib/name：即使同包有同名函数，也按全局解析。
    let mut kv = fresh_kv("shadow");
    compile(
        &mut kv,
        "rwfunc f(*int64 $x) -> (*int64 $r) { *x -> *r }\nlib p {\n    rwfunc f(*int64 $x) -> (*int64 $r) { *x -> *r }\n    rwfunc caller() -> (*int64 $r) { f(1) -> *r }\n}\n",
    )
    .unwrap();
}

#[test]
fn unknown_bare_call_is_rejected() {
    // 未实现的内建（如 debugger）：既不是全局函数、也未 def rwir 声明 → 明确报错，
    // 不放行到运行期（运行期同样 unknown rwir，只会把失败推迟）。
    let mut kv = fresh_kv("unknown");
    let err = compile(
        &mut kv,
        "rwfunc t() -> () {\n    debugger()\n}\n",
    )
    .unwrap_err();
    assert!(err.contains("未定义函数 debugger"), "{err}");
    assert!(err.contains("未给出被调函数的完整路径"), "{err}");
}
