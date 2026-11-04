//! uint64 与未定型整数字面量的 promotion 回归（spec 03-类型系统/01 第 10 条）。
//!
//! 这条路径靠 **字面量操作数落槽的 kind** 决定结果：删掉 typed opcode 后，若字面量
//! 仍落成 `int64`，通用 `compare_int` 只在两侧都 unsigned 时做无符号比较，
//! `18446744073709551615 > 1` 会变成 false、`max`/`÷` 也会给出有符号结果。
//!
//! 两级断言：
//! 1. 布局层（总是跑）：字面量槽 kind 是 uint64、编码值正确，写槽类型不是 int64；
//! 2. 执行层（`byteseek` 与运行时库缺失时跳过）：真跑一遍，断言输出值。

use kvlanglayout::{init_dirs, kvkind, Kv};
use std::path::{Path, PathBuf};
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

fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("layout crate has a parent")
        .to_path_buf()
}

fn temp_dir(tag: &str) -> TempDir {
    let dir = std::env::temp_dir().join(format!(
        "kvlanglayout_uint64_{}_{}",
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
    let b = kvkind::body(&v, &kvkind::head(&v));
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

/// 读参槽 kind + body 的前 8 字节小端值（内联整数标量）。
fn integer_slot(kv: &mut Kv, key: &str) -> (String, u64) {
    let v = kv.get_one(key);
    let b = kvkind::body(&v, &kvkind::head(&v)).to_vec();
    let mut raw = [0u8; 8];
    let n = b.len().min(8);
    raw[..n].copy_from_slice(&b[..n]);
    (kv.langtype_of(key), u64::from_le_bytes(raw))
}

/// 布局层用：不调用 println（未 boot 时 /lib 里还没有它）。
const LAYOUT_SRC: &str = "rwfunc test() -> () {\n\
    u:uint64 = 18446744073709551615\n\
    u > 1 -> gt\n\
    max(u, 1) -> big\n\
    u ÷ 2 -> half\n\
    e = max(u, 1)\n\
}\n";

/// 执行层用：加 println 把值打出来。
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

/// 布局层：字面量与写槽的 kind 必须是 promotion 结果（uint64），不是 int64。
#[test]
fn literal_and_result_slots_use_promoted_kind() {
    let dir = temp_dir("layout");
    let file = dir.join("p.kv");
    std::fs::write(&file, LAYOUT_SRC).unwrap();
    let db = format!("fs://{}", dir.join("db").display());
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(
        out.status.success(),
        "layout failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );

    let mut kv = Kv::conn(&db);
    init_dirs(&mut kv).unwrap();
    let prefix = "/lib/test/";

    // 每条指令的 opcode 只发语义名。
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
            // 写槽 body = `名字\0类型`。
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
    // 所有内联整数字面量都必须按 uint64 落槽：`1`、`2` 与 uint64 字面量本身。
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
    // `e = max(u, 1)` 的写槽类型必须是 uint64。
    assert!(
        write_kinds.iter().any(|t| t == "uint64"),
        "max 的写槽应推断为 uint64：{write_kinds:?}"
    );

}

/// 执行层：真跑一遍，断言值。缺 `byteseek` 或运行时库时跳过。
#[test]
fn uint64_comparison_and_max_yield_unsigned_values() {
    let root = repo_root();
    let term = std::env::var("KVLANG_TERM_BIN")
        .map(PathBuf::from)
        .unwrap_or_else(|_| root.join("bin/byteseek"));
    if !term.is_file() {
        eprintln!("skip: {} not built; run `make byteseek`", term.display());
        return;
    }
    let dir = temp_dir("exec");
    let file = dir.join("p.kv");
    std::fs::write(&file, RUN_SRC).unwrap();
    let dsn = format!("shm://{}", dir.join("db").display());
    let env = |key: &str, value: &str| {
        (
            key.to_string(),
            value.to_string(),
        )
    };
    let lib = format!(
        "{}:{}:{}",
        root.join("lib").display(),
        root.join("lib/kvspace").display(),
        root.join("bin").display()
    );
    let vars = [
        env("KVSPACE", &dsn),
        env("LD_LIBRARY_PATH", &lib),
        env("DYLD_FALLBACK_LIBRARY_PATH", &lib),
    ];
    let mut boot = Command::new(&term);
    boot.args(["-c", "print(\"boot\")"]);
    for (k, v) in &vars {
        boot.env(k, v);
    }
    let booted = boot.output().unwrap();
    /* runtimec2 是**按裁决冻结**在 8 字节 XValue ABI 上的（#622：C 迁移完成后删除），
     * 不随新 ABI 更新。于是本用例的前提——存在一个能按当前 wire 跑起来的 runtime——
     * **按设计不再成立**。
     *
     * 这与「依赖坏了」不同：坏是意外，冻结是决定。所以这里 skip 并说明缘由，免得
     * 有人把它读成「这个测试坏了」或「layout/ABI 坏了」。
     *
     * 判据要**正面识别冻结 runtime**，不能只看 boot 退出码：旧 runtime 读新 wire 时
     * stdlib 初始化会整片报 `unknown rwir` / `invalid XValue`，但进程仍以 0 退出——
     * 那时测试会拿错误的值去断言，把「runtime 冻结」报成「layout 算错」。 */
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

    assert!(out.status.success(), "run failed: {stderr}\nstdout:\n{stdout}");

    // 值断言：无符号比较为 true、max 取大者、÷2 得 2^63-1、动态槽不得打出 -1。
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
