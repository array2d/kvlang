//! 调试用：编译一个 .kv 文件，递归 dump /lib 树（key → kind）。
use std::env;
use std::fs;

use kvlang_layout::{compile, init_dirs, kvkind, Kv};

fn dump(kv: &mut Kv, prefix: &str) {
    for c in kv.list(prefix, false, true) {
        let full = format!("{prefix}{c}");
        let v = kv.get_one(&full);
        println!("{full}\tkind={} array_len={}", kvkind::kind(&v), kvkind::array_len(&v));
        if c.ends_with('/') {
            dump(kv, &full);
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let src = fs::read_to_string(&args[1]).unwrap();
    let mut kv = Kv::conn("redis://127.0.0.1:6379");
    init_dirs(&mut kv).unwrap();
    compile(&mut kv, &src).unwrap();
    dump(&mut kv, "/lib/");
}
