//! 调试用：编译一个 .kv 文件，dump /lib 树（key → kind:value，审查 lower 后的 code）。
//! 用法：dump <file.kv>    dsn 走 KVSPACE env（默认 redis://127.0.0.1:6379）
use std::env;
use std::fs;

use kvlanglayout::{compile, dump, init_dirs, Kv};

fn main() {
    let args: Vec<String> = env::args().collect();
    let src = fs::read_to_string(&args[1]).unwrap();
    let dsn = env::var("KVSPACE").unwrap_or_else(|_| "redis://127.0.0.1:6379".to_string());
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    match compile(&mut kv, &src) {
        Ok(()) => print!("{}", dump(&mut kv, "/lib")),
        Err(e) => eprintln!("compile error: {e}"),
    }
}
