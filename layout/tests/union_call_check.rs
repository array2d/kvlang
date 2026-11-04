//! 调用点 union 形参检查：实参类型必须是 union 成员之一（类型空洞的补丁）。

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
    // 写前删同名：异常退出会留下残骸，反复跑会累积。
    let _ = std::fs::remove_file(&path);
    std::fs::write(&path, src).unwrap();
    path
}

fn dsn(tag: &str) -> String {
    format!(
        "fs:///tmp/kvlanglayout_unionmsg_{}_{}",
        tag,
        std::process::id(),
    )
}

fn fresh_kv(tag: &str) -> Kv {
    let dsn = format!(
        "fs:///tmp/kvlanglayout_union_{}_{}",
        tag,
        std::process::id()
    );
    let _ = std::fs::remove_dir_all(dsn.trim_start_matches("fs://"));

    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    kv
}

#[test]
fn union_members_accepted() {
    let mut kv = fresh_kv("ok");
    compile(
        &mut kv,
        "rwfunc f(*int64|float64 $x) -> (*int64 $r) {\n    0 -> *r\n}\n",
    )
    .unwrap();
    let decl = "rwfunc f(*int64|float64 $x) -> (*int64 $r) {\n    0 -> *r\n}\n";
    compile(&mut kv, &format!("{decl}rwfunc g() -> () {{\n    f(1)\n}}\n")).unwrap();
    compile(&mut kv, &format!("{decl}rwfunc h() -> () {{\n    f(1.5)\n}}\n")).unwrap();
    compile(&mut kv, &format!("{decl}rwfunc i(*int64 $v) -> () {{\n    f(*v)\n}}\n")).unwrap();
}

#[test]
fn union_non_members_rejected() {
    let mut kv = fresh_kv("bad");
    compile(
        &mut kv,
        "rwfunc f(*int64|float64 $x) -> (*int64 $r) {\n    0 -> *r\n}\n",
    )
    .unwrap();
    let decl = "rwfunc f(*int64|float64 $x) -> (*int64 $r) {\n    0 -> *r\n}\n";
    // bool 字面量不在 union 成员内
    assert!(compile(&mut kv, &format!("{decl}rwfunc g() -> () {{\n    f(true)\n}}\n")).is_err());
    // 已声明 int32 变量不在 union 成员内（不做隐式加宽）
    assert!(compile(
        &mut kv,
        &format!("{decl}rwfunc h() -> () {{\n    a:int32 = 1\n    f(a)\n}}\n")
    )
    .is_err());
    // 字符串不在数值 union 内
    assert!(compile(
        &mut kv,
        &format!("{decl}rwfunc i() -> () {{\n    f(\"s\")\n}}\n")
    )
    .is_err());
}

#[test]
fn union_pointer_and_string_members() {
    let mut kv = fresh_kv("ptr");
    compile(
        &mut kv,
        "struct P {\n\tx:int64 = 0\n}\nrwfunc f(*P|int64 $p) -> () {\n}\n",
    )
    .unwrap();
    compile(&mut kv, "rwfunc g() -> () {\n    1\n    f(1)\n}\n").unwrap();
    compile(
        &mut kv,
        "rwfunc f2(*[]char/utf8|int64 $s) -> () {\n}\nrwfunc h() -> () {\n    f2(\"x\")\n}\n",
    )
    .unwrap();
}

#[test]
fn union_mismatch_reports_both_sides() {
    let file = temp_file(
        "bad",
        "rwfunc f(*int64|float64 $x) -> (*int64 $r) {\n    0 -> *r\n}\nrwfunc g() -> () {\n    f(true)\n}\n",
    );
    let db = dsn("bad");
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(!out.status.success());
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(
        err.contains("cannot pass bool to int64|float64 (parameter x)"),
        "stderr: {err}"
    );
}
