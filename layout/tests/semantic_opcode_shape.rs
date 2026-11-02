//! 验收：layout 只发语义算子名，`<kind>·<op>`（`int64·add`、`float64·lt`…）一律不再出现。
//!
//! 用独立 layout 进程跑一组覆盖各类型族的源码，逐条检查落库的 opcode 槽。

use kvlanglayout::{init_dirs, kvkind, Kv};
use std::path::PathBuf;
use std::process::Command;

const KINDS: [&str; 10] = [
    "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float32", "float64",
];
const OPS: [&str; 22] = [
    "add", "sub", "mul", "div", "mod", "max", "min", "neg", "abs", "sign", "eq", "neq", "lt", "le",
    "gt", "ge", "bitand", "bitor", "bitxor", "shl", "shr", "pow",
];

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_kvlanglayout")
}

fn temp_file(tag: &str, src: &str) -> PathBuf {
    let path = std::env::temp_dir().join(format!(
        "kvlanglayout_shape_{}_{}.kv",
        tag,
        std::process::id()
    ));
    std::fs::write(&path, src).unwrap();
    path
}

fn dsn(tag: &str) -> String {
    format!(
        "fs:///tmp/kvlanglayout_shape_{}_{}_{}",
        tag,
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    )
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v, &kvkind::head(&v));
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

/// 跑一次 layout，返回函数里所有 opcode 槽文本。
fn opcodes(tag: &str, func: &str, src: &str) -> Vec<String> {
    let file = temp_file(tag, src);
    let db = dsn(tag);
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(
        out.status.success(),
        "layout failed ({tag}): {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut kv = Kv::conn(&db);
    init_dirs(&mut kv).unwrap();
    let prefix = format!("/lib/{func}/");
    let mut ops = Vec::new();
    for c in kv.list(&prefix, false, false) {
        let t = c.trim_end_matches('/');
        if !t.starts_with('[') || !t.ends_with(",0]") {
            continue;
        }
        let text = slot_text(&mut kv, &format!("{prefix}{c}"));
        if text != "=" && !text.is_empty() {
            ops.push(text);
        }
    }
    ops
}

fn assert_semantic(tag: &str, src: &str) {
    for op in opcodes(tag, "f", src) {
        assert!(
            !(KINDS.iter().any(|k| op.starts_with(&format!("{k}·")))
                && OPS.iter().any(|o| op.ends_with(o))),
            "{tag}: 出现 typed opcode {op:?}"
        );
        assert!(
            !op.contains('·') || op.starts_with("string·") || op.starts_with("compactarray·")
                || op.starts_with("scatterarray·") || op.starts_with("kvspace·"),
            "{tag}: 出现非白名单的带 `·` opcode {op:?}"
        );
    }
}

#[test]
fn integer_families_emit_semantic_names() {
    assert_semantic(
        "i64",
        "rwfunc f(a:int64, b:int64) -> () {\n\ta + b -> w\n\ta - b -> w\n\ta × b -> w\n\ta ÷ b -> w\n\ta % b -> w\n\ta == b -> w\n\ta < b -> w\n\tabs(a) -> w\n\tsign(a) -> w\n\ta & b -> w\n\ta | b -> w\n\ta ^ b -> w\n\ta << b -> w\n\ta >> b -> w\n\tmax(a, b) -> w\n\tmin(a, b) -> w\n}\n",
    );
    assert_semantic(
        "u64",
        "rwfunc f(a:uint64, b:uint64) -> () {\n\ta + b -> w\n\ta < b -> w\n\tmax(a, b) -> w\n}\n",
    );
    assert_semantic(
        "small",
        "rwfunc f(a:int8, b:uint8) -> () {\n\ta + a -> w\n\tb + b -> w\n\ta < a -> w\n}\n",
    );
}

#[test]
fn float_families_emit_semantic_names() {
    assert_semantic(
        "f64",
        "rwfunc f(a:float64, b:float64) -> () {\n\ta + b -> w\n\ta - b -> w\n\ta × b -> w\n\ta ÷ b -> w\n\ta < b -> w\n\tsqrt(a) -> w\n\tpow(a, b) -> w\n\tabs(a) -> w\n\tneg(a) -> w\n\tmax(a, b) -> w\n}\n",
    );
    assert_semantic(
        "f32",
        "rwfunc f(a:float32, b:float32) -> () {\n\ta + b -> w\n\ta < b -> w\n\tmax(a, b) -> w\n}\n",
    );
}

#[test]
fn unary_minus_emits_neg() {
    let ops = opcodes("unary", "f", "rwfunc f(a:int64) -> () {\n\t-a -> w\n}\n");
    assert!(ops.iter().any(|o| o == "neg"), "一元负号应发 neg：{ops:?}");
    assert!(!ops.iter().any(|o| o.contains('·')), "{ops:?}");
}

#[test]
fn string_plus_emits_concat_and_mixed_is_rejected() {
    let ops = opcodes(
        "concat",
        "f",
        "rwfunc f() -> () {\n\ta = \"x\"\n\ta + \"y\" -> w\n}\n",
    );
    assert!(
        ops.iter().any(|o| o == "string·concat"),
        "字符串 + 应发 string·concat：{ops:?}"
    );
    assert!(!ops.iter().any(|o| o == "add" || o == "+"), "{ops:?}");

    // 混合字符串与数值在 layout 阶段被拒（不留运行时）。
    let file = temp_file("mixed", "rwfunc f() -> () {\n\ta = \"x\" + 1\n}\n");
    let db = dsn("mixed");
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(!out.status.success(), "混合字符串与数值必须 layout 失败");
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(err.contains("cannot mix string and numeric"), "stderr: {err}");
}

#[test]
fn no_switch_can_bring_typed_names_back() {
    // 开关已删：把历史开关值设成任意值都不影响产物。
    for value in ["1", "0", ""] {
        let file = temp_file("switch", "rwfunc f(a:int64) -> () {\n\ta + a -> w\n}\n");
        let db = dsn("switch");
        let out = Command::new(bin())
            .arg(&file)
            .arg(&db)
            .env("KVLANG_LAYOUT_SEMANTIC_OP", value)
            .env("KVLANG_LAYOUT_STRING_CONCAT", value)
            .output()
            .unwrap();
        assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
        let mut kv = Kv::conn(&db);
        init_dirs(&mut kv).unwrap();
        for c in kv.list("/lib/f/", false, false) {
            let t = c.trim_end_matches('/');
            if !t.starts_with('[') || !t.ends_with(",0]") {
                continue;
            }
            let text = slot_text(&mut kv, &format!("/lib/f/{c}"));
            assert!(
                !text.contains('·'),
                "环境变量 {value:?} 让 typed opcode 复活：{text:?}"
            );
        }
    }
}
