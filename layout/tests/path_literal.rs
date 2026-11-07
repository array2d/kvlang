//! 路径字面量：不带引号的 `/...` 是路径，带引号的 `"..."` 是字符串（不向后兼容）。
//! 规范：01-词法/05-绝对路径字面量、04-layout语义/10-常见诊断。
//! 引号串单独作写槽 → `string "…" is not a path; absolute paths must be unquoted`；
//! 引号 base + 成员链（`"/x"·*k`）是显式「字符串作路径」形态，仍接受。

use kvlanglayout::{compile, init_dirs, kvkind, Kv};
use std::process::Command;

/// store 守卫：离开作用域时删目录。先于 `Kv` 声明，故后 drop。
struct TempStore(String);

impl Drop for TempStore {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(self.0.trim_start_matches("fs://"));
    }
}

fn fresh_kv(tag: &str) -> (TempStore, Kv) {
    let dsn = format!(
        "fs:///tmp/kvlanglayout_pathlit_{}_{}",
        tag,
        std::process::id()
    );
    let store = TempStore(dsn.clone());
    let _ = std::fs::remove_dir_all(dsn.trim_start_matches("fs://"));

    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    (store, kv)
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}



#[test]
fn unquoted_path_write_is_kept() {
    let (_store, mut kv) = fresh_kv("unquoted");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    42 -> /tmp/pathlit/a\n}\n",
    )
    .unwrap();
    let text = slot_text(&mut kv, "/lib/f/[1,1]");
    assert!(text.starts_with("/tmp/pathlit/a"), "写槽应是路径：{text:?}");
}

#[test]
fn quoted_path_in_write_slot_is_rejected() {
    // 走 CLI 才能断言逐字诊断（规范 04-layout语义/10-常见诊断）。
    let bin = env!("CARGO_BIN_EXE_kvlanglayout");
    let file = std::env::temp_dir().join(format!(
        "kvlanglayout_pathlit_cli_{}.kv",
        std::process::id()
    ));
    std::fs::write(
        &file,
        "rwfunc f() -> () {\n    42 -> \"/tmp/pathlit/a\"\n}\n",
    )
    .unwrap();
    let dsn = format!(
        "fs:///tmp/kvlanglayout_pathlit_cli_{}",
        std::process::id()
    );
    let out = Command::new(bin)
        .arg(&file)
        .arg(&dsn)
        .output()
        .expect("run kvlanglayout");
    assert!(!out.status.success(), "引号路径写槽应被拒绝");
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(
        err.contains(
            "string \"/tmp/pathlit/a\" is not a path; absolute paths must be unquoted"
        ),
        "诊断文本不符：{err}"
    );
    let _ = std::fs::remove_file(&file);
    let _ = std::fs::remove_dir_all(dsn.trim_start_matches("fs://"));
}

#[test]
fn quoted_member_base_is_still_allowed() {
    let (_store, mut kv) = fresh_kv("member");
    compile(
        &mut kv,
        "rwfunc f(*int64 $i) -> () {\n    1 -> \"/tmp/pathlit\"·*i\n}\n",
    )
    .unwrap();
    let text = slot_text(&mut kv, "/lib/f/[1,1]");
    assert!(
        text.starts_with("/tmp/pathlit·*i"),
        "引号 base 成员写应保留路径形态：{text:?}"
    );
}

#[test]
fn quoted_string_read_is_a_string_value() {
    let (_store, mut kv) = fresh_kv("read");
    compile(&mut kv, "rwfunc f() -> () {\n    \"/tmp/pathlit\" -> v\n}\n").unwrap();
    let key = "/lib/f/[1,-1]";
    let v = kv.get_one(key);
    let lt = kv.langtype_of(key);
    assert!(
        kvkind::kind(&v, &lt).starts_with("char/"),
        "引号读应是字符值，kind={}",
        kvkind::kind(&v, &lt)
    );
}

#[test]
fn single_quoted_is_a_bare_literal_path() {
    // 单引号是「裸字面量，按上下文解释」（01-词法/07）：写槽位置按路径。
    let (_store, mut kv) = fresh_kv("single");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    42 -> '/tmp/pathlit/b'\n}\n",
    )
    .unwrap();
    let text = slot_text(&mut kv, "/lib/f/[1,1]");
    assert!(text.starts_with("/tmp/pathlit/b"), "单引号路径写槽：{text:?}");
}

#[test]
fn quoted_string_assignment_is_fine() {
    let (_store, mut kv) = fresh_kv("assign");
    compile(&mut kv, "rwfunc f() -> () {\n    x = \"/a/b\"\n}\n").unwrap();
}

#[test]
fn raw_quoted_path_in_write_slot_is_rejected() {
    // r"…" 是字符串（quote == b'r'）；判据写成 == b'"' 会漏掉它，必须有这条钉子。
    let bin = env!("CARGO_BIN_EXE_kvlanglayout");
    let file = std::env::temp_dir().join(format!(
        "kvlanglayout_pathlit_raw_{}.kv",
        std::process::id()
    ));
    std::fs::write(
        &file,
        "rwfunc f() -> () {\n    42 -> r\"/tmp/pathlit/c\"\n}\n",
    )
    .unwrap();
    let dsn = format!(
        "fs:///tmp/kvlanglayout_pathlit_raw_{}",
        std::process::id()
    );
    let out = Command::new(bin)
        .arg(&file)
        .arg(&dsn)
        .output()
        .expect("run kvlanglayout");
    assert!(!out.status.success(), "原始串写槽应被拒绝");
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(
        err.contains(
            "string \"/tmp/pathlit/c\" is not a path; absolute paths must be unquoted"
        ),
        "原始串诊断文本不符：{err}"
    );
    let _ = std::fs::remove_file(&file);
    let _ = std::fs::remove_dir_all(dsn.trim_start_matches("fs://"));
}

#[test]
fn unquoted_path_member_base_is_allowed() {
    // 无引号路径 base + 成员链：重排顺序时必须保持接受。
    let (_store, mut kv) = fresh_kv("pathbase");
    compile(
        &mut kv,
        "rwfunc f(*int64 $i) -> () {\n    1 -> /tmp/pathlit·*i\n}\n",
    )
    .unwrap();
    let text = slot_text(&mut kv, "/lib/f/[1,1]");
    assert!(
        text.starts_with("/tmp/pathlit·*i"),
        "无引号路径 base 成员写：{text:?}"
    );
}
