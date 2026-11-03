//! 波 0：参数声明语法与禁止隐式解引用（spec 04-layout语义/03-函数、05-runtime语义/04-函数调用）。
//!
//! 两条规约：
//!
//! 1. **参数声明只有两种形态**：`*T $name`（传址）与 `T $name`（标量值复制）。
//!    类型在前、`$name` 在后；`$` 只是声明的标记，体内引用写**裸名**。
//!    旧写法 `name: type` 不再接受。
//! 2. **禁止隐式解引用**：`·`、`·[i]`、`[i]` 作用在 `*T` 基上一律报错，必须写
//!    `(*p)·x` / `(*p)·[i]` / `(*p)[i]`。显式 `*` 是唯一解引用路径。
//!
//! 本文件是**判据**：先写测试再改 parser。每条正例给出波 0 后必须编过的输入，
//! 每条反例给出必须打红、且错误文本命中规约原文的输入。

use std::path::PathBuf;
use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_kvlanglayout")
}

fn temp_dir(tag: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "kvlanglayout_wave0_{}_{}",
        tag,
        std::process::id()
    ));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// 跑一次 `kvlanglayout vet`，返回 (是否通过, 诊断文本)。
fn check(tag: &str, src: &str) -> (bool, String) {
    let dir = temp_dir(tag);
    let file = dir.join("p.kv");
    std::fs::write(&file, src).unwrap();
    let out = Command::new(bin())
        .args(["vet", file.to_str().unwrap()])
        .output()
        .unwrap();
    let text = format!(
        "{}{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
    (out.status.success(), text)
}

fn assert_ok(tag: &str, src: &str) {
    let (ok, text) = check(tag, src);
    assert!(ok, "{tag} 应通过，实际诊断：\n{text}");
}

fn assert_err(tag: &str, src: &str, expected: &str) {
    let (ok, text) = check(tag, src);
    assert!(!ok, "{tag} 应被拒绝，却通过了");
    assert!(
        text.contains(expected),
        "{tag} 的错误文本要含 {expected:?}，实际：\n{text}"
    );
}

// ── 正例：波 0 后必须编过 ──────────────────────────────────────────

/// 新参数语法 `*T $name` / `T $name` 必须被接受，体内引用写裸名。
#[test]
fn new_parameter_syntax_is_accepted() {
    assert_ok(
        "newsyntax",
        "rwfunc add_one(int64 $n) -> (int64 $r) {\n\tn + 1 -> r\n}\n",
    );
}

/// 指针参数在体内用 `*name` 解引用后做成员访问：`(*p)·x`。
#[test]
fn explicit_deref_member_access_is_allowed() {
    assert_ok(
        "deref-member",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\n\
         rwfunc read(*Point $p) -> (int64 $r) {\n\t(*p)·x -> r\n}\n",
    );
}

/// 指针参数显式解引用后做坐标访问：`(*p)·[i]`。
#[test]
fn explicit_deref_coordinate_access_is_allowed() {
    assert_ok(
        "deref-coord",
        "rwfunc first(*int64·int64 $p) -> (int64 $r) {\n\t(*p)·[1] -> r\n}\n",
    );
}

// ── 反例：波 0 后必须打红，且文本命中规约原文 ────────────────────

/// 错误 1：`p·x`（p 为 *Point）必须报「对指针类型的成员访问必须显式解引用」。
#[test]
fn implicit_deref_on_member_access_is_rejected() {
    assert_err(
        "implicit-member",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\n\
         rwfunc read(*Point $p) -> (int64 $r) {\n\tp·x -> r\n}\n",
        "对指针类型的成员访问必须显式解引用",
    );
}

/// 错误 2：`p·[i]`（p 为 *map）必须报「对指针类型的坐标访问必须显式解引用」。
#[test]
fn implicit_deref_on_coordinate_access_is_rejected() {
    assert_err(
        "implicit-coord",
        "rwfunc first(*int64·int64 $p) -> (int64 $r) {\n\tp·[1] -> r\n}\n",
        "对指针类型的坐标访问必须显式解引用",
    );
}

/// 错误 4：旧写法 `name: type` 不再接受。
#[test]
fn old_parameter_syntax_is_rejected() {
    assert_err(
        "oldsyntax",
        "rwfunc add_one(n:int64) -> (r:int64) {\n\tn + 1 -> r\n}\n",
        "参数声明必须是 `*T $name` 或 `T $name`",
    );
}

/// 错误 5：值复制参数只能是字符串、数值或 bool；容器要写指针形式。
#[test]
fn value_copy_of_container_requires_pointer_form() {
    assert_err(
        "valuecopy-container",
        "rwfunc take(int64·int64 $p) -> (int64 $r) {\n\t1 -> r\n}\n",
        "值复制参数只能是字符串、数值或 bool",
    );
}

// ── 写槽：参数槽本身永远不可写 ────────────────────────────────────

/// 错误 6：裸写指针形参 = 改指针绑定，不是写目标。
#[test]
fn bare_write_to_pointer_parameter_is_rejected() {
    assert_err(
        "bare-write",
        "rwfunc f(*[]int64 $arr) -> (*[]int64 $acc) {\n\tscatterarray·append(*arr, 1) -> acc\n}\n",
        "写指针形参必须显式解引用",
    );
}

/// 正例：显式解引用写目标 `-> *acc` 合法。
#[test]
fn explicit_deref_write_target_is_allowed() {
    assert_ok(
        "deref-write",
        "rwfunc f(*[]int64 $arr) -> (*[]int64 $acc) {\n\tscatterarray·append(*arr, 1) -> *acc\n}\n",
    );
}

// ── 空 kindexpr 与值复制参数（spec 03-类型系统/07-文法与合法性:61）──────

/// 错误 7：无类型、无 `*` 的形参——值复制没有值可拷贝，形态未定义。
#[test]
fn untyped_value_copy_parameter_is_rejected() {
    assert_err(
        "untyped-value-copy",
        "rwfunc f($a) -> () {\n\t1 -> x\n}\n",
        "值复制参数必须声明类型",
    );
}

/// 错误 8：解引用值复制形参——它是只读副本，没有可写的目标。
#[test]
fn dereferencing_value_copy_parameter_is_rejected() {
    assert_err(
        "deref-value-copy",
        "rwfunc f(int64 $a) -> () {\n\tsub(*a) -> x\n}\n",
        "值复制参数不可解引用",
    );
}

/// 正例：`* $a` 是「不约束目标类型的指针形参」，体内写 `*a`。
#[test]
fn unconstrained_pointer_parameter_is_allowed() {
    assert_ok(
        "empty-kindexpr-ptr",
        "rwfunc f(* $a) -> () {\n\t*a + 1 -> x\n}\n",
    );
}
