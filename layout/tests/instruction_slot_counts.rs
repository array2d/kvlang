//! 指令行的计数头必须等于实际槽数（spec 02-kvspace模型/08-指令布局格式）。
//!
//! 规约原文：opcode 槽 body = 5 字节 counts `[nr:u16 LE][nw:u16 LE][dynamic:u8]` +
//! 目标名；`s1 = -j` 为第 j 个读槽、`+j` 为第 j 个写槽。故对每条指令恒有
//!
//!   nr == 存在的 `[n,-j]` 槽数，nw == 存在的 `[n,+j]` 槽数。
//!
//! 这条是 layout 的**输出契约**，不是某个算子的特例：任何算子声明了 N 个读参
//! 就必须落 N 个读槽，反之亦然。声明数与槽数不符时 runtime 会按声明数取到空槽
//! （取值缺失）或多读（越界），两种都是静默错值。
//!
//! 判据直接读 kvspace 里的 `[n,0]` 计数头与 `[n,±j]` 槽，不经 layout 的
//! 内部数据结构——layout 自己认为写了几个不构成证据。

use kvlanglayout::{init_dirs, Kv};
use std::path::PathBuf;
use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_kvlanglayout")
}

/// 测试用的临时目录，**离开作用域即删**（含 panic 展开时）。
///
/// 普通清理语句在断言失败时会被跳过——测试红了反而留下最多垃圾。
/// 这个守卫挂在 `Drop` 上，成功与失败都清。`/tmp` 曾因这类残留被撑爆。
struct TempDir(PathBuf);

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

impl TempDir {
    fn path(&self) -> &PathBuf {
        &self.0
    }

    fn join<P: AsRef<std::path::Path>>(&self, p: P) -> PathBuf {
        self.0.join(p)
    }
}

fn temp_dir(tag: &str) -> TempDir {
    let dir = std::env::temp_dir().join(format!(
        "kvlanglayout_slotcounts_{}_{}",
        tag,
        std::process::id()
    ));
    // 先删同名旧目录：上一次异常退出可能留了残骸。
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    TempDir(dir)
}

/// layout 一段源码到独立 fs store，返回已连接的 Kv。
fn compile(tag: &str, src: &str) -> (TempDir, Kv) {
    let dir = temp_dir(tag);
    let file = dir.join("p.kv");
    std::fs::write(&file, src).unwrap();
    let db = format!("fs://{}", dir.join("db").display());
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(
        out.status.success(),
        "layout failed ({tag}): {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut kv = Kv::conn(&db);
    init_dirs(&mut kv).unwrap();
    (dir, kv)
}

/// 目录下实际存在的读槽 / 写槽个数（列目录枚举，不预设上界）。
fn count_slots(kv: &mut Kv, dir: &str, instruction: usize) -> (usize, usize) {
    let mut reads = 0;
    let mut writes = 0;
    for child in kv.list(&format!("{dir}/"), false, false) {
        let name = child.trim_end_matches('/').trim_start_matches('[').to_string();
        let Some((row, col)) = name.split_once(',') else {
            continue;
        };
        let Ok(col) = col.trim_end_matches(']').parse::<i32>() else {
            continue;
        };
        if row.parse::<usize>().ok() != Some(instruction) || col == 0 {
            continue;
        }
        if col < 0 {
            reads += 1;
        } else {
            writes += 1;
        }
    }
    (reads, writes)
}

/// opcode 槽 `[n,0]` 声明的 (nr, nw)：body 前 5 字节是 `[nr:u16 LE][nw:u16 LE][dynamic:u8]`。
fn declared_counts(kv: &mut Kv, dir: &str, instruction: usize) -> (usize, usize) {
    let wire = kv.get_one(&format!("{dir}/[{instruction},0]"));
    assert!(wire.len() >= 21, "{dir}/[{instruction},0] 缺计数头");
    let body = &wire[16..];
    let nr = u16::from_le_bytes([body[0], body[1]]) as usize;
    let nw = u16::from_le_bytes([body[2], body[3]]) as usize;
    (nr, nw)
}

/// 断言一个函数目录里每条指令的声明数 == 实际槽数。
fn assert_counts_match(kv: &mut Kv, dir: &str, tag: &str) {
    let mut instruction = 1;
    loop {
        if kv.get_one(&format!("{dir}/[{instruction},0]")).is_empty() {
            break;
        }
        let (nr, nw) = declared_counts(kv, dir, instruction);
        let (reads, writes) = count_slots(kv, dir, instruction);
        assert_eq!(
            (reads, writes),
            (nr, nw),
            "{tag}: {dir}/[{instruction},0] 声明 nr={nr} nw={nw}，实际读槽 {reads} 个 / 写槽 {writes} 个"
        );
        instruction += 1;
    }
}

/// 验收：`struct·new(path, k1, v1, k2, v2)` 是 1 + 2×字段数 个读参，
/// 两个字段就是 5 个。声明 5 个就必须落 5 个读槽。
#[test]
fn struct_literal_with_two_fields_writes_every_argument_slot() {
    let (_dir, mut kv) = compile(
        "struct2",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\nlib p {\n\trwfunc f() -> () {\n\t\tq:Point = {x=3 y=4}\n\t}\n}\n",
    );
    assert_counts_match(&mut kv, "/lib/p·f", "two-field struct");
    assert_eq!(declared_counts(&mut kv, "/lib/p·f", 1), (5, 1));
    assert_eq!(count_slots(&mut kv, "/lib/p·f", 1), (5, 1));
}

/// 验收：字段更多时同样成立（6 字段 = 13 个读参），且上界不是写死的窗口。
#[test]
fn struct_literal_with_six_fields_writes_every_argument_slot() {
    let (_dir, mut kv) = compile(
        "struct6",
        "struct Big {\n\ta:int64=0\n\tb:int64=0\n\tc:int64=0\n\td:int64=0\n\te:int64=0\n\tf:int64=0\n}\nlib p {\n\trwfunc g() -> () {\n\t\tq:Big = {a=1 b=2 c=3 d=4 e=5 f=6}\n\t}\n}\n",
    );
    assert_counts_match(&mut kv, "/lib/p·g", "six-field struct");
    assert_eq!(declared_counts(&mut kv, "/lib/p·g", 1), (13, 1));
    assert_eq!(count_slots(&mut kv, "/lib/p·g", 1), (13, 1));
}

/// 验收：`map·new(v0, v1)` 是 2 个读参、2 个槽（这条本来就是对的，防回归）。
#[test]
fn map_new_stays_two_reads_two_slots() {
    let (_dir, mut kv) = compile(
        "map2",
        "lib p {\n\trwfunc m() -> () {\n\t\ta:int64·int64 = {1 2}\n\t}\n}\n",
    );
    assert_counts_match(&mut kv, "/lib/p·m", "map·new");
    assert_eq!(declared_counts(&mut kv, "/lib/p·m", 1), (2, 1));
    assert_eq!(count_slots(&mut kv, "/lib/p·m", 1), (2, 1));
}

/// 判据的自检：故意把计数头读错位置必须被发现，否则上面几条断言无法证明
/// 「声明数 != 槽数」真的能被区分出来。
#[test]
fn the_check_detects_a_mismatch_when_one_is_introduced() {
    let (_dir, mut kv) = compile(
        "detect",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\nlib p {\n\trwfunc f() -> () {\n\t\tq:Point = {x=3 y=4}\n\t}\n}\n",
    );
    assert_eq!(count_slots(&mut kv, "/lib/p·f", 1), (5, 1));

    // 直接改写 kvspace 里 opcode 槽的读参计数 5 → 3：模拟「声明数与槽数不符」。
    let key = "/lib/p·f/[1,0]";
    let langtype = kv.langtype_of(key);
    let mut mutated = kv.get_one(key);
    mutated[16..18].copy_from_slice(&3u16.to_le_bytes());
    kv.set(&[(key.to_string(), langtype, mutated)]).unwrap();

    assert_eq!(declared_counts(&mut kv, "/lib/p·f", 1), (3, 1));
    assert_eq!(count_slots(&mut kv, "/lib/p·f", 1), (5, 1));
}

/// 全语料普查：tutorial 每个 .kv 单独一个 store，每条指令的声明数都必须等于实际槽数。
///
/// 必须先把 runtime-c 跑起来发布 `/‥isa`（`byteseek -c 'print("boot")'`）——
/// layout 校验算子名要有这份能力表，否则 tutorial 里的 `println` 之类一律被判未定义。
/// 这与 `tutorial/test.py` 的 `_run_case` 一致。
#[test]
fn every_tutorial_instruction_has_matching_slot_counts() {
    let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .to_path_buf();
    let runtime = repo.join("bin").join("byteseek");
    // 缺 runtime 时**必须失败**：静默 `return` 会让「没跑」与「通过」在计数上不可区分，
    // 一次干净树复验因此得出 121 而非 120。没跑就是没跑，不能冒充绿。
    assert!(
        runtime.exists(),
        "缺 {}：无法 boot /‥isa；请先构建 byteseek",
        runtime.display()
    );

    let mut files = Vec::new();
    let mut stack = vec![repo.join("tutorial")];
    while let Some(path) = stack.pop() {
        for entry in std::fs::read_dir(&path).unwrap() {
            let path = entry.unwrap().path();
            if path.is_dir() {
                stack.push(path);
            } else if path.extension().is_some_and(|e| e == "kv") {
                files.push(path);
            }
        }
    }
    files.sort();
    // 用户待查清单里的文件跳过：它们是「原始形态 vs 迁移形态」的对照样本，
    // 迁移期不改，本判据也不该因为它们红。
    const PENDING_REVIEW: &[&str] = &[
        "01-basics/strings.kv",
        "08-leetcode/083_remove_dup_list.kv",
        "08-leetcode/094_inorder_traversal.kv",
        "08-leetcode/206_reverse_linked_list.kv",
        "12-struct/05-graph.kv",
        "12-struct/06-list-reverse.kv",
    ];
    files.retain(|p| {
        !PENDING_REVIEW
            .iter()
            .any(|tail| p.ends_with(format!("tutorial/{tail}")))
    });
    assert!(!files.is_empty(), "tutorial 下没有找到 .kv");

    // 语料取**工作树**：本判据要验的是「当前布局能否吃下当前语料」。
    // 早先为绕开别人在途的半成品而取 HEAD，那只是权宜——迁移落盘后工作树才是真相，
    // 取 HEAD 会让已迁移的文件仍按旧语法被拒。
    //
    // 但整棵树逐文件拷贝时要**跳过读不了的**：别人可能正写到一半（文件不存在或读失败），
    // 那不该让本判据红；那份文件本就不在 `files` 里。
    // 快照**保留相对路径**（`snapshot/tutorial/…/x.kv`），诊断里就能看出是哪个语料文件；
    // 扁平化成 `caseN.kv` 会让报错指向一个无意义的编号，把定位成本转给下一个人。
    let snapshot = temp_dir("corpus-worktree");
    let mut materialized = Vec::new();
    for file in files.iter() {
        let relative = file.strip_prefix(&repo).unwrap();
        let target = snapshot.join(relative);
        std::fs::create_dir_all(target.parent().unwrap()).unwrap();
        std::fs::copy(file, &target).unwrap();
        materialized.push(target);
    }
    let files = materialized;

    let root = temp_dir("corpus");
    let mut checked = 0;
    let mut skipped = Vec::new();
    for (index, file) in files.iter().enumerate() {
        let db = format!("fs://{}", root.join(format!("case{index}")).display());
        let boot = Command::new(&runtime)
            .args(["-c", "print(\"boot\")"])
            .env("KVSPACE", &db)
            .current_dir(&repo)
            .output()
            .unwrap();
        assert!(
            boot.status.success(),
            "{}: boot 失败 {}",
            file.display(),
            String::from_utf8_lossy(&boot.stderr)
        );
        let out = Command::new(bin())
            .arg(file)
            .arg(&db)
            .current_dir(&repo)
            .output()
            .unwrap();
        if !out.status.success() {
            // layout 失败的**唯一**可豁免原因是本机没装那条能力（`未定义函数 X`）：
            // 那时 layout 根本没产出指令，谈不上计数不符。除它之外一律红。
            let stderr = String::from_utf8_lossy(&out.stderr).into_owned();
            assert!(
                stderr.contains("未定义函数"),
                "{}: layout 失败且不是缺能力：{stderr}",
                file.display()
            );
            skipped.push(file.display().to_string());
            continue;
        }
        let mut kv = Kv::conn(&db);
        init_dirs(&mut kv).unwrap();
        for dir in function_dirs(&mut kv, "/lib/") {
            assert_counts_match(&mut kv, &dir, &file.display().to_string());
            checked += 1;
        }
        // 逐个删掉这份 store：250 个 fs 目录留着不删，一天几十轮就把 /tmp 撑爆过。
        drop(kv);
        let _ = std::fs::remove_dir_all(root.join(format!("case{index}")));
    }
    // 语料快照也一并清掉。
    assert!(checked > 0, "tutorial 一个函数目录都没扫到");
    eprintln!(
        "扫描 {checked} 个函数目录；{} 个用例缺能力跳过：{skipped:?}",
        skipped.len()
    );
}

/// 收集 `prefix` 下的函数目录（有指令槽的那些）。`list` 返回裸子名，故拼接父前缀。
fn function_dirs(kv: &mut Kv, prefix: &str) -> Vec<String> {
    let mut out = Vec::new();
    for child in kv.list(prefix, false, false) {
        let name = child.trim_end_matches('/').trim_end_matches(".src");
        let key = format!("{prefix}{name}");
        if kv.get_one(&format!("{key}/[1,0]")).is_empty() {
            continue;
        }
        out.push(key);
    }
    out
}
