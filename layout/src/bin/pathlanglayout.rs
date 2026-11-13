
use std::env;
use std::fs;

use pathlanglayout::{compile, format, init_dirs, printlib, vet, vet_with_store, Kv};

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

fn read_src(path: &str) -> String {
    fs::read_to_string(path).unwrap_or_else(|e| die(&format!("read {path}: {e}")))
}

fn require_dsn(arg: Option<&str>) -> String {
    match arg {
        Some(v) if !v.is_empty() => v.to_string(),
        _ => {
            eprintln!("缺少 DSN：pathlanglayout 只接受 shm://，且必须显式给出（无默认值）。");
            eprintln!("例：pathlanglayout hello.kv shm:///tmp/kv.shm");
            eprintln!("    pathlanglayout printlib hello.kv /lib shm:///tmp/kv.shm");
            std::process::exit(2);
        }
    }
}

fn die(msg: &str) -> ! {
    eprintln!("{msg}");
    std::process::exit(1);
}

fn layout_or_die(src: &str, dsn: &str) -> Kv {
    let mut kv = Kv::try_conn(dsn).unwrap_or_else(|e| die(&e));
    if let Err(e) = init_dirs(&mut kv) {
        die(&format!("init_dirs {dsn}: {e}"));
    }
    if let Err(e) = compile(&mut kv, src) {
        die(&format!("layout failed: {e}"));
    }
    kv
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() >= 3 && args[1] == "vet" {
        let src = read_src(&args[2]);
        
        let result = match args.get(3) {
            Some(dsn) => {
                let mut kv = Kv::conn(dsn);
                vet_with_store(&mut kv, &src)
            }
            None => vet(&src),
        };
        match result {
            Ok(()) => println!("ok"),
            Err(e) => {
                eprintln!("{e}");
                std::process::exit(1);
            }
        }
        return;
    }
    if args.len() >= 3 && args[1] == "format" {
        let src = read_src(&args[2]);
        match format(&src) {
            Ok(s) => print!("{s}"),
            Err(e) => {
                eprintln!("format 失败: {e}");
                std::process::exit(1);
            }
        }
        return;
    }
    if args.len() >= 3 && args[1] == "printlib" {
        let prefix = args.get(3).map(String::as_str).unwrap_or("/lib");
        let dsn = require_dsn(args.get(4).map(String::as_str));
        let src = read_src(&args[2]);
        let mut kv = layout_or_die(&src, &dsn);
        print!("{}", printlib(&mut kv, prefix));
        return;
    }
    if args.len() < 2 {
        eprintln!("usage: pathlanglayout <file.kv> [dsn]  |  pathlanglayout {{vet|format}} <file.kv>  |  pathlanglayout printlib <file.kv> [prefix] [dsn]");
        std::process::exit(1);
    }
    let dsn = require_dsn(args.get(2).map(String::as_str));
    let src = read_src(&args[1]);

    let mut kv = layout_or_die(&src, &dsn);

    let entry = find_entry(&mut kv, "/lib/", "");
    let entry = if entry.is_empty() { "init" } else { &entry };
    println!("ENTRY={entry}");
    eprintln!("[rust-layout] {} -> {} (entry={entry})", args[1], dsn);
}
