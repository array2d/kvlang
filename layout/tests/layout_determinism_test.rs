
use std::path::{Path, PathBuf};
use std::process::Command;

fn seed_builtins(dsn: &str) {
    
    let mut kv = pathlanglayout::Kv::conn(dsn);
    pathlanglayout::init_dirs(&mut kv).unwrap();
    for op in ["println", "print", "cerr"] {
        kv.set(&[(
            format!("/lib/{op}"),
            pathlanglayout::kvkind::KIND_DEF_RWIR.to_string(),
            pathlanglayout::kvkind::new_defrwir(1, 0, true).unwrap(),
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

#[ignore = "待改用 pathspace API 遍历 KV 空间：shm 下 KV 树不在文件系统布局里"]
#[test]
fn layout_is_deterministic_across_processes() {
    let bin = env!("CARGO_BIN_EXE_pathlanglayout");
    let src =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../benchmark/cases/nqueens/nqueens.kv");
    let pid = std::process::id();
    let mut first: Option<Vec<(String, Vec<u8>)>> = None;
    for run in 0..16 {
        let db = PathBuf::from(format!("/tmp/pathlanglayout_det_{pid}_{run}"));
        let _ = std::fs::remove_dir_all(&db);
        seed_builtins(&format!("shm://{}", db.display()));
        let out = Command::new(bin)
            .arg(&src)
            .arg(format!("shm://{}", db.display()))
            .output()
            .expect("run pathlanglayout");
        assert!(out.status.success(), "layout failed: {:?}", out);
        let t = tree(&db.join("lib"));
        let _ = std::fs::remove_dir_all(&db);
        match &first {
            None => first = Some(t),
            Some(f) => assert_eq!(f, &t, "第 {run} 次 layout 与第 0 次不一致"),
        }
    }
}
