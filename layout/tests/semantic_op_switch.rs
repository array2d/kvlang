//! KVLANG_LAYOUT_SEMANTIC_OP A/B 开关 + union 调用点报错信息（子进程）。
//!
//! 开关是进程级 OnceLock，只能靠跑独立 layout 进程来切换。

use kvlanglayout::{init_dirs, kvkind, Kv};
use std::path::PathBuf;
use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_kvlanglayout")
}

fn temp_file(tag: &str, src: &str) -> PathBuf {
    let path = std::env::temp_dir().join(format!(
        "kvlanglayout_semop_{}_{}.kv",
        tag,
        std::process::id()
    ));
    std::fs::write(&path, src).unwrap();
    path
}

fn dsn(tag: &str) -> String {
    format!(
        "fs:///tmp/kvlanglayout_semop_{}_{}_{}",
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

/// 返回 (opcode, 全部操作数槽文本)。
fn dump_ops(tag: &str, src: &str, semantic: bool) -> (String, Vec<String>) {
    let file = temp_file(tag, src);
    let db = dsn(tag);
    let mut cmd = Command::new(bin());
    cmd.arg(&file).arg(&db);
    if semantic {
        cmd.env("KVLANG_LAYOUT_SEMANTIC_OP", "1");
    }
    let out = cmd.output().unwrap();
    assert!(
        out.status.success(),
        "layout failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut kv = Kv::conn(&db);
    init_dirs(&mut kv).unwrap();
    let children = kv.list("/lib/t/", false, false);
    let mut opcode = String::new();
    let mut operands = Vec::new();
    for c in children {
        let name = c.trim_end_matches('/').to_string();
        let key = format!("/lib/t/{name}");
        let alloc = kv.get_one(&key);
        let kind = kvkind::kind(&alloc, &kv.langtype_of(&key));
        if !name.starts_with('[') || kind != "rwir" {
            continue;
        }
        let text = slot_text(&mut kv, &key);
        if name.ends_with(",0]") {
            if text == "add" || text.ends_with("·add") {
                opcode = text;
            }
        } else {
            operands.push(text);
        }
    }
    (opcode, operands)
}

#[test]
fn switch_emits_semantic_or_typed_names() {
    let src = "rwfunc t() -> (r:int64) {\n    acc = 0\n    acc = acc + 1\n    acc -> r\n}\n";
    let (typed, mut ops_a) = dump_ops("off", src, false);
    let (semantic, mut ops_b) = dump_ops("on", src, true);
    assert_eq!(typed, "int64·add");
    assert_eq!(semantic, "add");
    // 除算子槽文本外，操作数槽必须逐字节一致。
    ops_a.sort();
    ops_b.sort();
    assert_eq!(ops_a, ops_b);
}

/// 回归：语义算子名与同包 rwfunc 撞名（`eq` vs `lib string { rwfunc eq }`）时，
/// 算子必须仍按 rwir 落库，绝不能被裸名解析成函数调用（曾导致无限递归）。
#[test]
fn semantic_operator_never_resolves_as_call() {
    let src = "lib string {\n    rwfunc eq(a:[]char/utf32, b:[]char/utf32) -> (r:bool) {\n        false -> r\n    }\n}\n\
               rwfunc t() -> (r:bool) {\n    1 == 1 -> r\n}\n";
    let file = temp_file("collide", src);
    let db = dsn("collide");
    let out = Command::new(bin())
        .arg(&file)
        .arg(&db)
        .env("KVLANG_LAYOUT_SEMANTIC_OP", "1")
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "layout failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut kv = Kv::conn(&db);
    init_dirs(&mut kv).unwrap();
    let mut comparison = None;
    for c in kv.list("/lib/t/", false, false) {
        let name = c.trim_end_matches('/').to_string();
        if !name.starts_with('[') || !name.ends_with(",0]") {
            continue;
        }
        let key = format!("/lib/t/{name}");
        let alloc = kv.get_one(&key);
        let kind = kvkind::kind(&alloc, &kv.langtype_of(&key));
        let text = slot_text(&mut kv, &key);
        if text == "eq" || text.ends_with("·eq") {
            comparison = Some((kind, text));
        }
    }
    let (kind, text) = comparison.expect("no eq operator slot in /lib/t");
    assert_eq!(text, "eq");
    assert_eq!(kind, "rwir", "operator must be rwir, not a function call");
    // 同名 rwfunc 仍然存在（且是 rwfunc），证明不是靠删函数规避。
    let fn_key = "/lib/string·/eq/[0,0]";
    let fn_alloc = kv.get_one(fn_key);
    assert_eq!(
        kvkind::kind(&fn_alloc, &kv.langtype_of(fn_key)),
        "rwfunc"
    );
}
