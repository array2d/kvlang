//! 读 .kv 文件，用 Rust layout 检查语法并布局进 kvspace（默认 redis），并输出入口（ENTRY=...），
//! 供 Go runtime 执行验证。
//! 用法：
//!   kvlanglayout <file.kv> [dsn]              仅 layout，打印 ENTRY=<entry>（默认子命令）
//!   kvlanglayout vet <file.kv>                仅校验（parse+lower），打印 ok 或错误
//!   kvlanglayout format <file.kv>             格式化输出到 stdout
//!   kvlanglayout dump <file.kv> [prefix] [dsn]  layout 后把 /lib（或 prefix）子树 dump 为可运行 kvlang + 槽位注释

use std::env;
use std::fs;

use kvlanglayout::{compile, dump, format, init_dirs, vet, Kv};

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
    if args.len() >= 3 && args[1] == "vet" {
        let src = fs::read_to_string(&args[2]).expect("read file");
        match vet(&src) {
            Ok(()) => println!("ok"),
            Err(e) => {
                eprintln!("{e}");
                std::process::exit(1);
            }
        }
        return;
    }
    if args.len() >= 3 && args[1] == "format" {
        let src = fs::read_to_string(&args[2]).expect("read file");
        match format(&src) {
            Ok(s) => print!("{s}"),
            Err(e) => {
                eprintln!("format 失败: {e}");
                std::process::exit(1);
            }
        }
        return;
    }
    if args.len() >= 3 && args[1] == "dump" {
        let prefix = args.get(3).map(String::as_str).unwrap_or("/lib");
        let dsn = args
            .get(4)
            .map(String::as_str)
            .unwrap_or("redis://127.0.0.1:6379");
        let src = fs::read_to_string(&args[2]).expect("read file");
        let mut kv = Kv::conn(dsn);
        init_dirs(&mut kv).expect("init_dirs");
        compile(&mut kv, &src).expect("compile");
        print!("{}", dump(&mut kv, prefix));
        return;
    }
    if args.len() < 2 {
        eprintln!("usage: kvlanglayout <file.kv> [dsn]  |  kvlanglayout {{vet|format}} <file.kv>  |  kvlanglayout dump <file.kv> [prefix] [dsn]");
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
