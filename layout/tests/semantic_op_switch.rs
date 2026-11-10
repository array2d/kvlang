//! 语义算子发射：layout 只发 `add`/`eq` 这类语义名，typed `int64·add` 已删除。
//!
//! 开关（`KVLANG_LAYOUT_SEMANTIC_OP`）随本改动一并删除，这里改为跑一次 layout 进程
//! 直接断言落库 opcode。

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
    // 写前删同名：异常退出会留下残骸，反复跑会累积。
    let _ = std::fs::remove_file(&path);
    std::fs::write(&path, src).unwrap();
    path
}

fn dsn(tag: &str) -> String {
    kvlanglayout::ffi::test_dsn(&format!("semop_{tag}"))
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

/// 返回 (opcode, 全部操作数槽文本)。
fn dump_ops(tag: &str, src: &str) -> (String, Vec<String>) {
    let file = temp_file(tag, src);
    let db = dsn(tag);
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
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
            if text == "add" {
                opcode = text;
            }
        } else {
            operands.push(text);
        }
    }
    // 用完即清：`.kv` 与 fs store 都删，反复跑不留残骸。
    drop(kv);
    let _ = std::fs::remove_file(&file);
    kvlanglayout::ffi::remove_test_store(&db);
    (opcode, operands)
}

#[test]
fn emits_semantic_names_only() {
    let src = "rwfunc t() -> (*int64 $r) {\n    acc = 0\n    acc = acc + 1\n    acc -> *r\n}\n";
    let (opcode, _) = dump_ops("sem", src);
    assert_eq!(opcode, "add");
    assert!(!opcode.contains('·'), "typed opcode 不应再出现：{opcode}");
}

/// 回归：语义算子名与同包 rwfunc 撞名（`eq` vs `lib string { rwfunc eq }`）时，
/// 算子必须仍按 rwir 落库，绝不能被裸名解析成函数调用（曾导致无限递归）。
#[test]
fn semantic_operator_never_resolves_as_call() {
    let src = "lib string {\n    rwfunc eq(*[]char/utf32 $a, *[]char/utf32 $b) -> (*bool $r) {\n        false -> *r\n    }\n}\nrwfunc t() -> (*bool $r) {\n    1 == 1 -> *r\n}\n";
    let file = temp_file("collide", src);
    let db = dsn("collide");
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
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
        if text == "eq" {
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
    drop(kv);
    let _ = std::fs::remove_file(&file);
    kvlanglayout::ffi::remove_test_store(&db);
}
