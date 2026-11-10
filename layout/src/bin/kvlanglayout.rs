//! 读 .kv 文件，用 Rust layout 检查语法并布局进 kvspace（默认 redis），并输出入口（ENTRY=...），
//! 供 Go runtime 执行验证。
//! 用法：
//!   kvlanglayout <file.kv> [dsn]              仅 layout，打印 ENTRY=<entry>（默认子命令）
//!   kvlanglayout vet <file.kv>                仅校验（parse+lower），打印 ok 或错误
//!   kvlanglayout format <file.kv>             格式化输出到 stdout
//!   kvlanglayout printlib <file.kv> [prefix] [dsn]  layout 后把 /lib（或 prefix）子树重建为可运行 kvlang + 槽位注释（不读 .src）

use std::env;
use std::fs;

use kvlanglayout::{compile, format, init_dirs, printlib, vet, vet_with_store, Kv};

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

/// Read source file, printing an error and exiting 1 on failure.
fn read_src(path: &str) -> String {
    fs::read_to_string(path).unwrap_or_else(|e| die(&format!("read {path}: {e}")))
}

/// DSN **必填**，缺失即报错退出——**不回落任何默认值**。
///
/// 曾经这里 `unwrap_or("redis://127.0.0.1:6379")`：不给 DSN 就静默连本机 redis。
/// 那种默认值的危害不在它存在，而在它没人记得自己存在——单库化之后 `libkvspace`
/// 只服务 `shm://`，那个默认值根本连不上，却要等到连的时候才炸，报错也不指向这里。
///
/// 报错给一条能直接照抄的命令，而不是只说「缺 DSN」。
fn require_dsn(arg: Option<&str>) -> String {
    match arg {
        Some(v) if !v.is_empty() => v.to_string(),
        _ => {
            eprintln!("缺少 DSN：kvlanglayout 只接受 shm://，且必须显式给出（无默认值）。");
            eprintln!("例：kvlanglayout hello.kv shm:///tmp/kv.shm");
            eprintln!("    kvlanglayout printlib hello.kv /lib shm:///tmp/kv.shm");
            std::process::exit(2);
        }
    }
}

fn die(msg: &str) -> ! {
    eprintln!("{msg}");
    std::process::exit(1);
}

/// Connect, initialize root dirs, and compile; exits 1 on any error.
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
        // `vet <file> [dsn]`：给了 dsn 就按 `compile` 的判据再跑一遍依赖 `/lib` 声明的检查。
        // 不给就只做纯检查——此时「ok」**不**等于「能编译」，见 `code::vet` 的注释。
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
        eprintln!("usage: kvlanglayout <file.kv> [dsn]  |  kvlanglayout {{vet|format}} <file.kv>  |  kvlanglayout printlib <file.kv> [prefix] [dsn]");
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
