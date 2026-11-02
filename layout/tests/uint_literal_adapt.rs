//! 未定型整数字面量适配（spec 03-类型系统/01-种类与定宽类型 第 10 条）：
//! `uint64(5) + 1` 的 `1` 按上下文适配到 uint64，不算混合确定性符号；
//! 两侧都是确定类型且符号冲突（`uint64_var + int64_var`）仍不特化（运行时 TypeError）。

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
        assert!(
            os.iter().any(|o| o == "uint64·add"),
            "应特化为 uint64·add：{os:?} src={src}"
        );
        assert!(
            !os.iter().any(|o| o == "int64·add"),
            "不得特化为 int64·add：{os:?} src={src}"
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
    let os = ops(&mut kv, "f");
    assert!(os.iter().any(|o| o == "uint8·add"), "255 应适配 uint8：{os:?}");

    let mut kv = fresh_kv("overflow");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint8 = 200\n    u + 256 -> r\n}\n",
    )
    .unwrap();
    let os = ops(&mut kv, "f");
    assert!(
        !os.iter().any(|o| o == "uint8·add"),
        "256 超出 uint8，不得适配：{os:?}"
    );
    assert!(os.iter().any(|o| o == "+"), "应保持通用 +：{os:?}");
}

#[test]
fn float_still_wins_over_literal_adaptation() {
    let mut kv = fresh_kv("float");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u + 1.0 -> r\n}\n",
    )
    .unwrap();
    let os = ops(&mut kv, "f");
    assert!(
        os.iter().any(|o| o == "float64·add"),
        "浮点在场应提升为 float64·add：{os:?}"
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
    assert!(
        os.iter().any(|o| o == "uint64·add"),
        "两个无符号应按宽度加宽为 uint64·add：{os:?}"
    );
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
        os.iter().any(|o| o == "+"),
        "两侧都是确定类型且符号冲突，应保持通用 +：{os:?}"
    );
    assert!(
        !os.iter().any(|o| o.contains('·') && o.ends_with("add")),
        "不得特化：{os:?}"
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
    let os = ops(&mut kv, "f");
    assert!(os.iter().any(|o| o == "uint64·eq"), "u == 1 应适配：{os:?}");

    let mut kv = fresh_kv("cmpneg");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u == -1 -> r\n}\n",
    )
    .unwrap();
    let os = ops(&mut kv, "f");
    assert!(
        !os.iter().any(|o| o == "uint64·eq"),
        "-1 不能适配 uint64，应保持通用比较：{os:?}"
    );
}
