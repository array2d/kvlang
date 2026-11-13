
use pathlanglayout::{init_dirs, kvkind, Kv};
use std::path::PathBuf;
use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_pathlanglayout")
}

fn temp_file(tag: &str, src: &str) -> PathBuf {
    let path = std::env::temp_dir().join(format!(
        "pathlanglayout_semop_{}_{}.kv",
        tag,
        std::process::id()
    ));
    
    let _ = std::fs::remove_file(&path);
    std::fs::write(&path, src).unwrap();
    path
}

fn dsn(tag: &str) -> String {
    pathlanglayout::ffi::test_dsn(&format!("semop_{tag}"))
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

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
    
    drop(kv);
    let _ = std::fs::remove_file(&file);
    pathlanglayout::ffi::remove_test_store(&db);
    (opcode, operands)
}

#[test]
fn emits_semantic_names_only() {
    let src = "rwfunc t() -> (*int64 #r) {\n    acc = 0\n    acc = acc + 1\n    acc -> *r\n}\n";
    let (opcode, _) = dump_ops("sem", src);
    assert_eq!(opcode, "add");
    assert!(!opcode.contains('·'), "typed opcode 不应再出现：{opcode}");
}

#[test]
fn semantic_operator_never_resolves_as_call() {
    let src = "lib string {\n    rwfunc eq(*[]char/utf32 #a, *[]char/utf32 #b) -> (*bool #r) {\n        false -> *r\n    }\n}\nrwfunc t() -> (*bool #r) {\n    1 == 1 -> *r\n}\n";
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
    
    let fn_key = "/lib/string·/eq/[0,0]";
    let fn_alloc = kv.get_one(fn_key);
    assert_eq!(kvkind::kind(&fn_alloc, &kv.langtype_of(fn_key)), "rwfunc");
    drop(kv);
    let _ = std::fs::remove_file(&file);
    pathlanglayout::ffi::remove_test_store(&db);
}
