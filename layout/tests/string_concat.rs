
use pathlanglayout::{init_dirs, kvkind, Kv};
use std::path::PathBuf;
use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_pathlanglayout")
}

fn temp_file(tag: &str, src: &str) -> PathBuf {
    let path = std::env::temp_dir().join(format!(
        "pathlanglayout_concat_{}_{}.kv",
        tag,
        std::process::id()
    ));
    
    let _ = std::fs::remove_file(&path);
    std::fs::write(&path, src).unwrap();
    path
}

fn dsn(tag: &str) -> String {
    pathlanglayout::ffi::test_dsn(&format!("concat_{tag}"))
}

struct Fixture {
    file: std::path::PathBuf,
    db: String,
    out: std::process::Output,
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.file);
        pathlanglayout::ffi::remove_test_store(&self.db);
    }
}

fn layout(tag: &str, src: &str, envs: &[(&str, &str)]) -> Fixture {
    let file = temp_file(tag, src);
    let db = dsn(tag);
    let mut cmd = Command::new(bin());
    cmd.arg(&file).arg(&db);
    for (k, v) in envs {
        cmd.env(k, v);
    }
    let out = cmd.output().unwrap();
    Fixture { file, db, out }
}

fn op_texts(db: &str) -> Vec<(String, String)> {
    let mut kv = Kv::conn(db);
    init_dirs(&mut kv).unwrap();
    let mut out = Vec::new();
    for c in kv.list("/lib/t/", false, false) {
        let name = c.trim_end_matches('/').to_string();
        if !name.starts_with('[') || !name.ends_with(",0]") {
            continue;
        }
        let key = format!("/lib/t/{name}");
        let alloc = kv.get_one(&key);
        let kind = kvkind::kind(&alloc, &kv.langtype_of(&key));
        let b = kvkind::body(&alloc);
        let text = String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned();
        out.push((kind, text));
    }
    out
}

const SRC: &str =
    "rwfunc t() -> (*char/utf32 #r) {\n    a = \"hello\"\n    b = a + \"!\"\n    b -> *r\n}\n";

#[test]
fn string_plus_becomes_concat() {
    let fx = layout("on", SRC, &[]);
    let (db, out) = (&fx.db, &fx.out);
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let ops = op_texts(&db);
    assert!(
        ops.iter().any(|(k, t)| k == "rwir" && t == "string·concat"),
        "expected string·concat rwir, got {ops:?}"
    );
}

#[test]
fn mixed_string_and_number_is_layout_error() {
    let src = "rwfunc t() -> (*char/utf32 #r) {\n    a = \"x\"\n    b = a + 1\n    b -> *r\n}\n";
    let fx = layout("mix", src, &[]);
    let out = &fx.out;
    assert!(!out.status.success());
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(
        err.contains("cannot mix string and numeric operands"),
        "stderr: {err}"
    );
}
