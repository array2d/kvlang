//! 操作数槽写**具体** langtype（decision-encoder-gaps §4）。
//!
//! encoder 只能把参数收敛到单一具体类型；声明的 def langtype 可能是并集
//! （`int64|float64`），无法收敛。故 layout 在槽上写静态可判定的具体 kind：
//!
//! 1. 声明本身是具体类型 → 写它（最强约束，不变）；
//! 2. 声明是并集、但赋值来源能证明唯一具体 kind → 写该 kind；
//! 3. 推不出 → 保留并集，由 encoder 拒绝（不猜精度）。

use kvlanglayout::{init_dirs, kvkind, Kv};
use std::path::{Path, PathBuf};
use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_kvlanglayout")
}

fn temp_dir(tag: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "kvlanglayout_slottypes_{}_{}",
        tag,
        std::process::id()
    ));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v, &kvkind::head(&v));
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

/// 读参槽（`[n,-j]`）的 langtype：变量引用是 body 里 NUL 之后的类型串。
fn read_slot_type(kv: &mut Kv, key: &str) -> String {
    let text = slot_text(kv, key);
    match text.rsplit_once('\0') {
        Some((_, ty)) => ty.to_string(),
        None => kv.langtype_of(key),
    }
}

fn compile(tag: &str, src: &str) -> (PathBuf, Kv) {
    let dir = temp_dir(tag);
    let file = dir.join("p.kv");
    std::fs::write(&file, src).unwrap();
    let db = format!("fs://{}", dir.join("db").display());
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(
        out.status.success(),
        "layout failed ({tag}): {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut kv = Kv::conn(&db);
    init_dirs(&mut kv).unwrap();
    (dir, kv)
}

/// 验收 1：`add(a:int64, b:float64)` 两个读槽分别是 int64 / float64。
#[test]
fn mixed_precision_params_keep_their_own_types() {
    let (_dir, mut kv) = compile(
        "mixed",
        "lib p {\n\trwfunc f(a:int64, b:float64) -> (r:float64) {\n\t\ta + b -> r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv, "/lib/p·f/[1,-1]"), "int64");
    assert_eq!(read_slot_type(&mut kv, "/lib/p·f/[1,-2]"), "float64");
}

/// 验收 2a：声明是并集、赋值来源全是同一具体 kind → 槽写具体。
#[test]
fn union_declaration_with_single_assignment_kind_is_narrowed() {
    let (_dir, mut kv) = compile(
        "narrow",
        "lib p {\n\trwfunc h() -> (r:int64) {\n\t\ty:int64|float64 = 1\n\t\ty + 1 -> r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv, "/lib/p·h/[2,-1]"), "int64");
    // 循环累加：boot 从字面量 0 点火，再由算术来源接上。
    let (_dir2, mut kv2) = compile(
        "loop",
        "lib p {\n\trwfunc s(n:int64) -> (r:int64|float64) {\n\t\ttotal:int64|float64 = 0\n\t\ti = 0\n\t\twhile (i < n) {\n\t\t\ttotal = total + i\n\t\t\ti = i + 1\n\t\t}\n\t\ttotal -> r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv2, "/lib/p·s/[6,-1]"), "int64");
}

/// 验收 2b：推不出具体类型时保留并集，交给 encoder 拒绝。
#[test]
fn unresolvable_union_is_preserved() {
    // 参数本身是并集：没有任何赋值来源能证明它具体是哪一支。
    let (_dir, mut kv) = compile(
        "param",
        "lib p {\n\trwfunc g(x:int64|float64) -> (r:float64) {\n\t\tx + 1.0 -> r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv, "/lib/p·g/[1,-1]"), "int64|float64");

    // 两条路径写不同 kind：真动态，保留并集。
    let (_dir2, mut kv2) = compile(
        "branch",
        "lib p {\n\trwfunc m(c:bool) -> (r:float64) {\n\t\tx:int64|float64 = 1\n\t\tif (c) {\n\t\t\tx = 2.5\n\t\t}\n\t\tx + 1.0 -> r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv2, "/lib/p·m/[7,-1]"), "int64|float64");
}

/// 具体类型不得越过声明契约：并集之外的 kind 不采纳。
#[test]
fn narrowing_never_invents_a_kind_outside_the_union() {
    let (_dir, mut kv) = compile(
        "outside",
        "lib p {\n\trwfunc f() -> (r:int8) {\n\t\ty:int8|int16 = 1\n\t\ty + int8(1) -> r\n\t}\n}\n",
    );
    // `y` 的唯一赋值是字面量 1（int64），不在 int8|int16 并集内 → 不得采纳。
    let ty = read_slot_type(&mut kv, "/lib/p·f/[3,-1]");
    assert!(
        ty == "int8|int16" || ty == "int8",
        "只允许并集成员，实际 {ty:?}"
    );
    assert!(!ty.contains("int64"), "不得把并集外的 int64 写上去：{ty:?}");
}
