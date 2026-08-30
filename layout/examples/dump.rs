//! 调试用：编译一个 .kv 文件，dump /lib 树（key → kind:value，审查 lower 后的 code）。
use std::env;
use std::fs;

use kvlanglayout::{compile, dump, init_dirs, Kv};

fn main() {
    let args: Vec<String> = env::args().collect();
    let src = fs::read_to_string(&args[1]).unwrap();
    let mut kv = Kv::conn("redis://127.0.0.1:6379");
    init_dirs(&mut kv).unwrap();
    compile(&mut kv, &src).unwrap();
    print!("{}", dump(&mut kv, "/lib"));
}
