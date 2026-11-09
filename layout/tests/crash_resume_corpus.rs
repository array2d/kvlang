//! Tally of `‥crash_resume` states over `tutorial/` and `runtimec/tests/kv/` (ignored by default).
//!
//! KVLANG_CORPUS_DIR=<dir> cargo test --release --test crash_resume_corpus -- --ignored --nocapture
//! Each file gets its own `shm://<dir>/<n>/store.shm` (default dir:
//! `<repo>/workspace/tmp/crash-resume-corpus`). `KVLANG_CORPUS_VERBOSE=1` lists every function.
//! `println`/`print`/`cerr` are declared first, as `runtimec/tests/kv_runner.c` does.

use kvlanglayout::{compile, init_dirs, kvkind, Kv};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

const CAPABILITIES: &str = "rwir println(*[]char/utf32 $s...)\n\
                            rwir print(*[]char/utf32 $s...)\n\
                            rwir cerr(*[]char/utf32 $s...)\n";

const STORE_FILES: &[&str] = &[
    "store.shm",
    "store.shm.garb",
    "store.shm.gen",
    "store.shm.sbo.data",
    "store.shm.sbo.head",
];

fn kv_files(root: &Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(root) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            kv_files(&path, out);
        } else if path.extension().is_some_and(|e| e == "kv") {
            out.push(path);
        }
    }
}

/// (function dir, record) for every laid-out function under `prefix` (walk as `printlib` does:
/// `<base>/` directories and `<base>·` member containers).
fn records(kv: &mut Kv, prefix: &str, out: &mut Vec<(String, String)>) {
    for child in kv.list(prefix, false, true) {
        let base = child.trim_end_matches('/');
        if base.is_empty() || base.contains("\u{2025}") {
            continue;
        }
        let wire = kv.get_one(&format!("{prefix}{base}/\u{2025}crash_resume"));
        if !wire.is_empty() {
            out.push((format!("{prefix}{base}"), kvkind::value_string(&wire)));
            continue;
        }
        for sub in [format!("{prefix}{base}/"), format!("{prefix}{base}\u{b7}")] {
            if !kv.list(&sub, false, true).is_empty() {
                records(kv, &sub, out);
            }
        }
    }
}

#[test]
#[ignore = "corpus tally; run explicitly"]
fn crash_resume_corpus_tally() {
    let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .to_path_buf();
    let work = std::env::var("KVLANG_CORPUS_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo.join("workspace/tmp/crash-resume-corpus"));
    let verbose = std::env::var("KVLANG_CORPUS_VERBOSE").is_ok_and(|v| v == "1");
    let mut files = Vec::new();
    kv_files(&repo.join("tutorial"), &mut files);
    kv_files(&repo.join("runtimec/tests/kv"), &mut files);
    files.sort();
    let mut states: BTreeMap<String, usize> = BTreeMap::new();
    let mut reasons: BTreeMap<String, usize> = BTreeMap::new();
    let (mut failed, mut functions) = (0usize, 0usize);
    for (index, file) in files.iter().enumerate() {
        let relative = file.strip_prefix(&repo).unwrap().display().to_string();
        let dir = work.join(index.to_string());
        let _ = std::fs::create_dir_all(&dir);
        let src = std::fs::read_to_string(file).unwrap();
        {
            let mut kv = Kv::conn(&format!("shm://{}/store.shm", dir.display()));
            init_dirs(&mut kv).unwrap();
            compile(&mut kv, CAPABILITIES).unwrap();
            match compile(&mut kv, &src) {
                Err(e) => {
                    failed += 1;
                    if verbose {
                        println!("FAILED {relative}: {}", e.lines().next().unwrap_or(""));
                    }
                }
                Ok(_) => {
                    let mut found = Vec::new();
                    records(&mut kv, "/lib/", &mut found);
                    for (key, record) in found {
                        functions += 1;
                        let mut parts = record.splitn(3, ':');
                        let state = parts.next().unwrap_or("").to_string();
                        if state == "NOT_RESUMABLE" {
                            let reason = parts.nth(1).unwrap_or("");
                            let kind = if reason.starts_with('/') {
                                "<absolute key>"
                            } else {
                                reason
                            };
                            let kind = kind.split(':').next().unwrap_or(kind);
                            *reasons.entry(kind.to_string()).or_default() += 1;
                        }
                        *states.entry(state).or_default() += 1;
                        if verbose {
                            println!("{relative} {key} {record}");
                        }
                    }
                }
            }
        }
        for name in STORE_FILES {
            let _ = std::fs::remove_file(dir.join(name));
        }
        let _ = std::fs::remove_dir(&dir);
    }
    println!(
        "files={} layout_failed={failed} functions={functions}",
        files.len()
    );
    for (state, count) in &states {
        println!("{state} {count}");
    }
    for (reason, count) in &reasons {
        println!("  NOT reason {reason} {count}");
    }
    assert!(functions > 0, "no function laid out");
}
