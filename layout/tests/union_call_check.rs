//! 调用点 union 形参检查：实参类型必须是 union 成员之一（类型空洞的补丁）。
//!
//! 2026-11-05：`rwfunc` 参数禁止并集后，本文件改用 **`rwir` 声明**承载 union——
//! 「调用点实参必须是 union 成员」这条能力作用于 rwir 声明，行为不变。

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

/// store 守卫：离开作用域时删目录。必须**先于 `Kv` 声明**，
/// 这样它后 drop（drop 顺序与声明顺序相反），删目录时句柄已释放。
struct TempStore(String);

impl Drop for TempStore {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
    }
}

fn fresh_kv(tag: &str) -> (TempStore, Kv) {
    let dsn = format!(
        "fs:///tmp/kvlanglayout_union_{}_{}",
        tag,
        std::process::id()
    );
    let store = TempStore(dsn.clone());
    let _ = std::fs::remove_dir_all(dsn.trim_start_matches("fs://"));

    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    (store, kv)
}

#[test]
fn union_members_accepted() {
    let (_store, mut kv) = fresh_kv("ok");
    let decl = "rwir f(*int64|float64 $x) -> (*int64 $r)\n";
    compile(&mut kv, decl).unwrap();
    compile(&mut kv, &format!("{decl}rwfunc g() -> () {{\n    f(1)\n}}\n")).unwrap();
    compile(&mut kv, &format!("{decl}rwfunc h() -> () {{\n    f(1.5)\n}}\n")).unwrap();
    compile(&mut kv, &format!("{decl}rwfunc i(*int64 $v) -> () {{\n    f(*v)\n}}\n")).unwrap();
}

#[test]
fn union_non_members_rejected() {
    let (_store, mut kv) = fresh_kv("bad");
    let decl = "rwir f(*int64|float64 $x) -> (*int64 $r)\n";
    compile(&mut kv, decl).unwrap();
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
    let (_store, mut kv) = fresh_kv("ptr");
    compile(
        &mut kv,
        "struct P {\n\tx:int64 = 0\n}\nrwir f(*P|int64 $p) -> () \n",
    )
    .unwrap();
    compile(&mut kv, "rwfunc g() -> () {\n    1\n    f(1)\n}\n").unwrap();
    compile(&mut kv, "rwir f2(*[]char/utf8|int64 $s) -> ()\n").unwrap();
    compile(&mut kv, "rwfunc h() -> () {\n    f2(\"x\")\n}\n").unwrap();
}

#[test]
fn union_mismatch_reports_both_sides() {
    let file = temp_file(
        "bad",
        "rwir f(*int64|float64 $x) -> (*int64 $r)\nrwfunc g() -> () {\n    f(true)\n}\n",
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
    let _ = std::fs::remove_dir_all(db.trim_start_matches("fs://"));
}
