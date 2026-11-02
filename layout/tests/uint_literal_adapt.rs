//! 未定型整数字面量适配（spec 03-类型系统/01-种类与定宽类型 第 10 条）：
//! `uint64(5) + 1` 的 `1` 按上下文适配到 uint64，不算混合确定性符号；
//! 两侧都是确定类型且符号冲突（`uint64_var + int64_var`）保持通用算子（运行时 TypeError）。
//!
//! opcode 只发语义名（`add`/`eq`），所以这里断言的是**字面量操作数槽的 kind**。

use kvlanglayout::{compile, init_dirs, kvkind, Kv};

fn fresh_kv(tag: &str) -> Kv {
    let dsn = format!(
        "fs:///tmp/kvlanglayout_uintlit_{}_{}_{}",
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

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let lt = kv.langtype_of(key);
    let h = kvkind::head(&v);
    let b = kvkind::body(&v, &h);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

/// 收集函数里所有 opcode 槽（[i,0]）的文本。
fn ops(kv: &mut Kv, func: &str) -> Vec<String> {
    let prefix = format!("/lib/{func}/");
    let children = kv.list(&prefix, false, false);
    let mut out = Vec::new();
    for c in children {
        let t = c.trim_end_matches('/');
        if !t.starts_with('[') || !t.ends_with(",0]") {
            continue;
        }
        out.push(slot_text(kv, &format!("{prefix}{c}")));
    }
    out
}


/// 字面量操作数落槽用的 kind：读参槽 body 是裸字面量文本时，其 attr langtype
/// 就是该字面量的 kind（变量读参的 attr langtype 恒为 `rwir`）。
fn literal_kinds(kv: &mut Kv, func: &str) -> Vec<String> {
    let prefix = format!("/lib/{func}/");
    let mut out = Vec::new();
    for c in kv.list(&prefix, false, false) {
        let t = c.trim_end_matches('/');
        if !t.starts_with('[') || !t.contains(",-") {
            continue;
        }
        let key = format!("{prefix}{c}");
        let lt = kv.langtype_of(&key);
        if !lt.is_empty() && lt != "rwir" {
            out.push(lt);
        }
    }
    out
}

/// 写槽的声明类型（rwir 槽 body = `名字\0类型`）。
fn write_kind(kv: &mut Kv, key: &str) -> String {
    let text = slot_text(kv, key);
    text.rsplit('\0').next().unwrap_or("").to_string()
}

#[test]
fn cast_plus_literal_adapts_to_unsigned() {
    for src in [
        "rwfunc f() -> () {\n    uint64(5) + 1 -> r\n}\n",
        "rwfunc f() -> () {\n    1 + uint64(5) -> r\n}\n",
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u + 1 -> r\n}\n",
    ] {
        let mut kv = fresh_kv("cast");
        compile(&mut kv, src).unwrap();
        let os = ops(&mut kv, "f");
        assert!(os.iter().any(|o| o == "add"), "应发语义算子 add：{os:?} src={src}");
        assert!(
            literal_kinds(&mut kv, "f").contains(&"uint64".to_string()),
            "字面量操作数应按 uint64 落槽：src={src}"
        );
    }
}

#[test]
fn write_slot_kind_matches_specialized_op() {
    let mut kv = fresh_kv("kind");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    uint64(5) + 1 -> r\n}\n",
    )
    .unwrap();
    // [2] 是 add；写槽 [2,1] 的 kind 必须是 uint64（不能算对却记 int64）。
    let k = write_kind(&mut kv, "/lib/f/[2,1]");
    assert_eq!(k, "uint64", "写槽 kind 应为 uint64，实际 {k}");
}

#[test]
fn uint8_literal_boundary_fits_and_overflows() {
    let mut kv = fresh_kv("bound");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint8 = 200\n    u + 255 -> r\n}\n",
    )
    .unwrap();
    assert!(
        literal_kinds(&mut kv, "f").contains(&"uint8".to_string()),
        "255 应按 uint8 落槽"
    );

    let mut kv = fresh_kv("overflow");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint8 = 200\n    u + 256 -> r\n}\n",
    )
    .unwrap();
    assert!(
        !literal_kinds(&mut kv, "f").contains(&"uint8".to_string()),
        "256 超出 uint8，不得适配"
    );
}

#[test]
fn float_still_wins_over_literal_adaptation() {
    let mut kv = fresh_kv("float");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u + 1.0 -> r\n}\n",
    )
    .unwrap();
    assert!(
        literal_kinds(&mut kv, "f").contains(&"float64".to_string()),
        "浮点在场应提升为 float64"
    );
}

#[test]
fn two_unsigned_widths_still_widen() {
    let mut kv = fresh_kv("widen");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    a:uint64 = 5\n    b:uint8 = 1\n    a + b -> r\n}\n",
    )
    .unwrap();
    let os = ops(&mut kv, "f");
    assert!(os.iter().any(|o| o == "add"), "应发语义算子 add：{os:?}");
}

#[test]
fn confirmed_mixed_signs_stay_untyped() {
    let mut kv = fresh_kv("mixed");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    a:uint64 = 5\n    b:int64 = 1\n    a + b -> r\n}\n",
    )
    .unwrap();
    let os = ops(&mut kv, "f");
    assert!(
        os.iter().any(|o| o == "add" || o == "+"),
        "应发通用算子（add/+）：{os:?}"
    );
    assert!(!os.iter().any(|o| o.contains('·')), "不得发 typed opcode：{os:?}");
    assert!(
        !literal_kinds(&mut kv, "f").contains(&"uint64".to_string()),
        "符号冲突时不得把字面量适配成无符号"
    );
}

#[test]
fn comparison_with_literal_adapts_but_negative_does_not() {
    let mut kv = fresh_kv("cmp");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u == 1 -> r\n}\n",
    )
    .unwrap();
    assert!(
        literal_kinds(&mut kv, "f").contains(&"uint64".to_string()),
        "u == 1 应适配 uint64"
    );

    let mut kv = fresh_kv("cmpneg");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u == -1 -> r\n}\n",
    )
    .unwrap();
    assert!(
        !literal_kinds(&mut kv, "f").contains(&"uint64".to_string()),
        "-1 不能适配 uint64，应保持通用比较"
    );
}
