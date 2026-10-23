//! layout 确定性：同一输入在**不同进程**里多次 layout，/lib 子树必须逐字节一致。
//!
//! stable_kinds 的 fixpoint 曾按 HashMap 随机序遍历（每进程哈希种子不同），
//! 导致同一语料多次 layout 的特化集合不同（特化数在 881/882/907 间波动）。
//! 单进程内 HashMap 顺序稳定，故必须用子进程复现。

use std::path::{Path, PathBuf};
use std::process::Command;

fn tree(dir: &Path) -> Vec<(String, Vec<u8>)> {
    fn walk(base: &Path, cur: &Path, out: &mut Vec<(String, Vec<u8>)>) {
        let mut ents: Vec<_> = std::fs::read_dir(cur)
            .unwrap()
            .map(|e| e.unwrap())
            .collect();
        ents.sort_by_key(|e| e.file_name());
        for e in ents {
            let p = e.path();
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

#[test]
fn layout_is_deterministic_across_processes() {
    let bin = env!("CARGO_BIN_EXE_kvlanglayout");
    let src = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../benchmark/cases/nqueens/nqueens.kv");
    let pid = std::process::id();
    let mut first: Option<Vec<(String, Vec<u8>)>> = None;
    for run in 0..16 {
        let db = PathBuf::from(format!("/tmp/kvlanglayout_det_{pid}_{run}"));
        let _ = std::fs::remove_dir_all(&db);
        let out = Command::new(bin)
            .arg(&src)
            .arg(format!("fs://{}", db.display()))
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
