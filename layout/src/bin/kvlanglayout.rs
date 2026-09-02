//! 读 .kv 文件，用 Rust layout 编译进 kvspace（默认 redis），并输出入口（ENTRY=...），
//! 供 Go runtime 执行验证。
//! 用法：kvlanglayout <file.kv> [dsn]

use std::env;
use std::fs;

use kvlanglayout::{compile, init_dirs, Kv};

/// 复刻 Go runtime 的 findEntry：DFS /lib/ 找首个 init（顶层 `init` 或 lib 块内 `pkg·init`）。
fn find_entry(kv: &mut Kv, prefix: &str, pkg: &str) -> String {
    let children = kv.list(prefix, false, true);
    for c in &children {
        let base = c.trim_end_matches('/');
        if base.ends_with(".src") {
            continue;
        }
        let full = if pkg.is_empty() {
            base.to_string()
        } else {
            format!("{pkg}·{base}")
        };
        if base == "init" {
            return full;
        }
        let next_pkg = if pkg.is_empty() {
            base.to_string()
        } else {
            format!("{pkg}/{base}")
        };
        let sub = if c.ends_with('/') {
            format!("{prefix}{base}/")
        } else {
            format!("{prefix}{base}·")
        };
        let entry = find_entry(kv, &sub, &next_pkg);
        if !entry.is_empty() {
            return entry;
        }
    }
    String::new()
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: kvlanglayout <file.kv> [dsn]");
        std::process::exit(1);
    }
    let dsn = args
        .get(2)
        .map(String::as_str)
        .unwrap_or("redis://127.0.0.1:6379");
    let src = fs::read_to_string(&args[1]).expect("read file");

    let mut kv = Kv::conn(dsn);
    init_dirs(&mut kv).expect("init_dirs");
    compile(&mut kv, &src).expect("compile");

    let entry = find_entry(&mut kv, "/lib/", "");
    let entry = if entry.is_empty() { "init" } else { &entry };
    println!("ENTRY={entry}");
    eprintln!("[rust-layout] {} -> {} (entry={entry})", args[1], dsn);
}
