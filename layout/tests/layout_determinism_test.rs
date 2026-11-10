//! layout 确定性：同一输入在**不同进程**里多次 layout，/lib 子树必须逐字节一致。
//!
//! stable_kinds 的 fixpoint 曾按 HashMap 随机序遍历（每进程哈希种子不同），
//! 导致同一语料多次 layout 的特化集合不同（特化数在 881/882/907 间波动）。
//! 单进程内 HashMap 顺序稳定，故必须用子进程复现。

use std::path::{Path, PathBuf};
use std::process::Command;

fn seed_builtins(dsn: &str) {
    // 裸名必须有声明来源：运行时 boot 会发布这些内建的 /lib/<op> def rwir。
    let mut kv = kvlanglayout::Kv::conn(dsn);
    kvlanglayout::init_dirs(&mut kv).unwrap();
    for op in ["println", "print", "cerr"] {
        kv.set(&[(
            format!("/lib/{op}"),
            kvlanglayout::kvkind::KIND_DEF_RWIR.to_string(),
            kvlanglayout::kvkind::new_defrwir(1, 0, true).unwrap(),
        )])
        .unwrap();
    }
}

fn tree(dir: &Path) -> Vec<(String, Vec<u8>)> {
    fn walk(base: &Path, cur: &Path, out: &mut Vec<(String, Vec<u8>)>) {
        let mut ents: Vec<_> = std::fs::read_dir(cur)
            .unwrap()
            .map(|e| e.unwrap())
            .collect();
        ents.sort_by_key(|e| e.file_name());
        for e in ents {
            let p = e.path();
            if p.file_name().is_some_and(|n| n == "‥ctime") {
                continue;
            }
            if p.is_dir() {
                walk(base, &p, out);
            } else {
                out.push((
                    p.strip_prefix(base).unwrap().to_string_lossy().into_owned(),
                    std::fs::read(&p).unwrap(),
                ));
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, &mut out);
    out
}



// 已知缺口 + 明确原因：旧判据依赖 **fs 的物理布局**，不是「换个后端」能替换的。
//
// `fs://` 下 KV 树就是文件系统目录树，所以 `tree()` 用 `read_dir` 递归比对成立。
// shm 下 KV 树在 mmap 文件**内部**，文件系统上只有 `store`、`.garb`、`.sbo.data`
// 这些物理文件——`read_dir` 对它们直接 `NotADirectory`。
//
// 要验同样的东西，得改成「用 kvspace API 遍历 KV 空间再比对」，那是换判据，
// 不在本轮范围。这里显式 `#[ignore]` 而不是留一个「跑起来恒过」的分支：
// 后者会让 `cargo test` 把它算进 passed，掩盖「这条没验」。
#[ignore = "待改用 kvspace API 遍历 KV 空间：shm 下 KV 树不在文件系统布局里"]
#[test]
fn layout_is_deterministic_across_processes() {
    let bin = env!("CARGO_BIN_EXE_kvlanglayout");
    let src =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../benchmark/cases/nqueens/nqueens.kv");
    let pid = std::process::id();
    let mut first: Option<Vec<(String, Vec<u8>)>> = None;
    for run in 0..16 {
        let db = PathBuf::from(format!("/tmp/kvlanglayout_det_{pid}_{run}"));
        let _ = std::fs::remove_dir_all(&db);
        seed_builtins(&format!("shm://{}", db.display()));
        let out = Command::new(bin)
            .arg(&src)
            .arg(format!("shm://{}", db.display()))
            .output()
            .expect("run kvlanglayout");
        assert!(out.status.success(), "layout failed: {:?}", out);
        let t = tree(&db.join("lib"));
        let _ = std::fs::remove_dir_all(&db);
        match &first {
            None => first = Some(t),
            Some(f) => assert_eq!(f, &t, "第 {run} 次 layout 与第 0 次不一致"),
        }
    }
}
