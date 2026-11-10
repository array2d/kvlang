//! 操作数槽写**具体** langtype（decision-encoder-gaps §4）。
//!
//! encoder 只能把参数收敛到单一具体类型；声明的 def langtype 可能是并集
//! （`int64|float64`），无法收敛。故 layout 在槽上写静态可判定的具体 kind：
//!
//! 1. 声明本身是具体类型 → 写它（最强约束，不变）；
//! 2. 声明是并集、但赋值来源能证明唯一具体 kind → 写该 kind；
//! 3. 推不出 → 保留并集，由 encoder 拒绝（不猜精度）。

use kvlanglayout::{init_dirs, kvkind, Kv};
use std::path::PathBuf;
use std::process::Command;


/// 测试用的临时目录，**离开作用域即删**（含 panic 展开时）。
/// 普通清理语句在断言失败时会被跳过——测试红了反而留下最多垃圾。
struct TempDir(PathBuf);

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

impl TempDir {
    fn join<P: AsRef<std::path::Path>>(&self, p: P) -> PathBuf {
        self.0.join(p)
    }
}



fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_kvlanglayout")
}

fn temp_dir(tag: &str) -> TempDir {
    let dir = std::env::temp_dir().join(format!(
        "kvlanglayout_slottypes_{}_{}",
        tag,
        std::process::id()
    ));
    // 先删同名旧目录：测试在同一 pod 反复跑会累积，/tmp 曾因此 OOM。
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    TempDir(dir)
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v);
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

fn compile(tag: &str, src: &str) -> (TempDir, Kv) {
    let dir = temp_dir(tag);
    let file = dir.join("p.kv");
    std::fs::write(&file, src).unwrap();
    let db = format!("shm://{}", dir.join("db").display());
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

/// 验收 1：`*a + *b` 的两个解引用读槽分别是 int64 / float64。
///
/// 新参数模型下形参是 `*T`，`*a` / `*b` 各自展开成一条拷贝指令（`_1 = *a`、
/// `_2 = *b`），所以两个读槽落在**不同行**的 `[·,-1]`，不是同一行的 `-1`/`-2`。
#[test]
fn mixed_precision_params_keep_their_own_types() {
    let (_dir, mut kv) = compile(
        "mixed",
        "lib p {\n\trwfunc f(*int64 $a, *float64 $b) -> (*float64 $r) {\n\t\t*a + *b -> *r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv, "/lib/p·f/[1,-1]"), "int64");
    assert_eq!(read_slot_type(&mut kv, "/lib/p·f/[2,-1]"), "float64");
}

/// 验收 2a：声明是并集、赋值来源全是同一具体 kind → 槽写具体。
#[test]
fn union_declaration_with_single_assignment_kind_is_narrowed() {
    let (_dir, mut kv) = compile(
        "narrow",
        "lib p {\n\trwfunc h() -> (*int64 $r) {\n\t\ty:int64|float64 = 1\n\t\ty + 1 -> *r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv, "/lib/p·h/[2,-1]"), "int64");
    // 循环累加：boot 从字面量 0 点火，再由算术来源接上。
    let (_dir2, mut kv2) = compile(
        "loop",
        "lib p {\n\trwfunc s(*int64 $n) -> () {\n\t\ttotal:int64|float64 = 0\n\t\ti = 0\n\t\twhile (i < *n) {\n\t\t\ttotal = total + i\n\t\t\ti = i + 1\n\t\t}\n\t}\n}\n",
    );
    // `while (i < *n)` 里的 `*n` 展开成一条拷贝指令，循环体因此右移到第 7 行。
    assert_eq!(read_slot_type(&mut kv2, "/lib/p·s/[7,-1]"), "int64");
}

/// 验收 2b：推不出具体类型时保留并集，交给 encoder 拒绝。
///
/// 旧版本还有一条「参数本身是并集」的用例；`rwfunc` 参数禁止并集之后
/// （spec 03-类型系统/05-langtype签名类型表达式）它不再是合法 kvlang，
/// 覆盖由下面这条「两条路径写不同 kind」承担。
#[test]
fn unresolvable_union_is_preserved() {
    // 两条路径写不同 kind：真动态，保留并集。
    let (_dir2, mut kv2) = compile(
        "branch",
        "lib p {\n\trwfunc m(*bool $c) -> (*float64 $r) {\n\t\tx:int64|float64 = 1\n\t\tif (*c) {\n\t\t\tx = 2.5\n\t\t}\n\t\tx + 1.0 -> *r\n\t}\n}\n",
    );
    // `if (*c)` 里的 `*c` 展开成一条拷贝指令，`x + 1.0` 因此右移到第 8 行。
    assert_eq!(read_slot_type(&mut kv2, "/lib/p·m/[8,-1]"), "int64|float64");
}

/// 具体类型不得越过声明契约：并集之外的 kind 不采纳。
#[test]
fn narrowing_never_invents_a_kind_outside_the_union() {
    let (_dir, mut kv) = compile(
        "outside",
        "lib p {\n\trwfunc f() -> (*int8 $r) {\n\t\ty:int8|int16 = 1\n\t\ty + int8(1) -> *r\n\t}\n}\n",
    );
    // `y` 的唯一赋值是字面量 1（int64），不在 int8|int16 并集内 → 不得采纳。
    let ty = read_slot_type(&mut kv, "/lib/p·f/[3,-1]");
    assert!(
        ty == "int8|int16" || ty == "int8",
        "只允许并集成员，实际 {ty:?}"
    );
    assert!(!ty.contains("int64"), "不得把并集外的 int64 写上去：{ty:?}");
}
