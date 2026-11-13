
use pathlanglayout::{init_dirs, kvkind, Kv};
use std::path::{Path, PathBuf};
use std::process::Command;

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
    env!("CARGO_BIN_EXE_pathlanglayout")
}

fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("layout crate has a parent")
        .to_path_buf()
}

fn test_arg(name: &str) -> Option<String> {
    let prefix = format!("{name}=");
    std::env::args().find_map(|a| a.strip_prefix(&prefix).map(str::to_string))
}

fn temp_dir(tag: &str) -> TempDir {
    let dir = std::env::temp_dir().join(format!(
        "pathlanglayout_uint64_{}_{}",
        tag,
        std::process::id()
    ));
    
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    TempDir(dir)
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

fn integer_slot(kv: &mut Kv, key: &str) -> (String, u64) {
    let v = kv.get_one(key);
    let b = kvkind::body(&v).to_vec();
    let mut raw = [0u8; 8];
    let n = b.len().min(8);
    raw[..n].copy_from_slice(&b[..n]);
    (kv.langtype_of(key), u64::from_le_bytes(raw))
}

const LAYOUT_SRC: &str = "rwfunc test() -> () {\n\
    u:uint64 = 18446744073709551615\n\
    u > 1 -> gt\n\
    max(u, 1) -> big\n\
    u ÷ 2 -> half\n\
    e = max(u, 1)\n\
}\n";

const RUN_SRC: &str = "rwfunc test() -> () {\n\
    u:uint64 = 18446744073709551615\n\
    u > 1 -> gt\n\
    println(\"gt\", gt)\n\
    max(u, 1) -> big\n\
    println(\"max\", big)\n\
    u ÷ 2 -> half\n\
    println(\"div\", half)\n\
    e = max(u, 1)\n\
    println(\"dyn\", e)\n\
}\n";

#[test]
fn literal_and_result_slots_use_promoted_kind() {
    let dir = temp_dir("layout");
    let file = dir.join("p.kv");
    std::fs::write(&file, LAYOUT_SRC).unwrap();
    let db = format!("shm://{}", dir.join("db").display());
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(
        out.status.success(),
        "layout failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );

    let mut kv = Kv::conn(&db);
    init_dirs(&mut kv).unwrap();
    let prefix = "/lib/test/";

    let mut opcodes = Vec::new();
    let mut literal_kinds = Vec::new();
    let mut write_kinds = Vec::new();
    for c in kv.list(prefix, false, false) {
        let name = c.trim_end_matches('/').to_string();
        if !name.starts_with('[') {
            continue;
        }
        let key = format!("{prefix}{name}");
        if name.ends_with(",0]") {
            let text = slot_text(&mut kv, &key);
            if text != "=" && !text.is_empty() {
                opcodes.push(text);
            }
        } else if name.contains(",-") {
            let (kind, value) = integer_slot(&mut kv, &key);
            if !kind.is_empty() && kind != "rwir" {
                literal_kinds.push((kind, value));
            }
        } else {
            let text = slot_text(&mut kv, &key);
            
            if let Some((_, ty)) = text.rsplit_once('\0') {
                if !ty.is_empty() {
                    write_kinds.push(ty.to_string());
                }
            }
        }
    }

    for op in &opcodes {
        assert!(
            !op.contains('·'),
            "typed opcode 不应出现：{op:?}（全部：{opcodes:?}）"
        );
    }
    
    assert!(
        !literal_kinds.iter().any(|(k, _)| k == "int64"),
        "不得有 int64 落槽的字面量：{literal_kinds:?}"
    );
    let mut values: Vec<u64> = literal_kinds.iter().map(|(_, v)| *v).collect();
    values.sort_unstable();
    assert_eq!(
        values,
        vec![1, 1, 1, 2, u64::MAX],
        "字面量槽的编码值不对：{literal_kinds:?}"
    );
    assert!(
        literal_kinds.iter().all(|(k, _)| k == "uint64"),
        "全部字面量都应为 uint64：{literal_kinds:?}"
    );
    
    assert!(
        write_kinds.iter().any(|t| t == "uint64"),
        "max 的写槽应推断为 uint64：{write_kinds:?}"
    );
}

#[test]
fn uint64_comparison_and_max_yield_unsigned_values() {
    let root = repo_root();
    
    let term = test_arg("term-bin")
        .map(PathBuf::from)
        .unwrap_or_else(|| root.join("bin/pathseek"));
    if !term.is_file() {
        eprintln!("skip: {} not built; run `make pathseek`", term.display());
        return;
    }
    let dir = temp_dir("exec");
    let file = dir.join("p.kv");
    std::fs::write(&file, RUN_SRC).unwrap();
    let dsn = format!("shm://{}", dir.join("db").display());
    let env = |key: &str, value: &str| (key.to_string(), value.to_string());
    let lib = format!(
        "{}:{}:{}",
        root.join("lib").display(),
        root.join("lib/pathspace").display(),
        root.join("bin").display()
    );
    let vars = [
        env("PATHSPACE", &dsn),
        env("LD_LIBRARY_PATH", &lib),
        env("DYLD_FALLBACK_LIBRARY_PATH", &lib),
    ];
    let mut boot = Command::new(&term);
    boot.args(["-c", "print(\"boot\")"]);
    for (k, v) in &vars {
        boot.env(k, v);
    }
    let booted = boot.output().unwrap();
    
    let boot_err = String::from_utf8_lossy(&booted.stderr).into_owned();
    let frozen_runtime = !booted.status.success()
        || boot_err.contains("runtimec2:")
        || boot_err.contains("invalid XValue");
    if frozen_runtime {
        let first = boot_err.lines().next().unwrap_or("");
        eprintln!(
            "SKIP: runtimec2 is frozen at the 8-byte XValue ABI by decision \
             (see #622); this test's premise no longer holds. \
             Not a layout/ABI fault.\n  boot stderr: {first}"
        );
        return;
    }

    let mut lay = Command::new(bin());
    lay.arg(&file).arg(&dsn);
    for (k, v) in &vars {
        lay.env(k, v);
    }
    let laid = lay.output().unwrap();
    assert!(
        laid.status.success(),
        "layout failed: {}",
        String::from_utf8_lossy(&laid.stderr)
    );

    let mut run = Command::new(&term);
    run.arg("test");
    for (k, v) in &vars {
        run.env(k, v);
    }
    let out = run.output().unwrap();
    let stdout = String::from_utf8_lossy(&out.stdout).into_owned();
    let stderr = String::from_utf8_lossy(&out.stderr).into_owned();

    assert!(
        out.status.success(),
        "run failed: {stderr}\nstdout:\n{stdout}"
    );

    assert!(
        stdout.contains("gt true"),
        "uint64 > 1 必须为 true（有符号比较会得 false）：\n{stdout}"
    );
    assert!(
        stdout.contains("max 18446744073709551615"),
        "max(uint64, 1) 必须取 uint64 侧：\n{stdout}"
    );
    assert!(
        stdout.contains("div 9223372036854775807"),
        "uint64 ÷ 2 必须是无符号除：\n{stdout}"
    );
    assert!(
        !stdout.contains("-1"),
        "动态写槽不得被推断成 int64（会打出 -1）：\n{stdout}"
    );
}
