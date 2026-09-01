//! rwir `internet/proc·exec`：同步执行外部命令到结束，返回退出码。
//!   internet/proc·exec(args, envs) -> exitcode
//! args/envs 均为 stringkeymap（成员 ·[i] 为 []char/utf32）：args 首成员=可执行文件、
//! 其余为参数；envs 每成员一条 "K=V"，空 = 继承父进程环境、非空 = 完整替换（execve 语义）。
//! exitcode：uint8。正常退出=退出码(0-255)；被信号终止=128+signo；spawn 失败/无 argv=127
//! （对齐 shell "command not found"，落入 uint8）。set_tlv_encoded 直编 uint8 单字节。
//! MVP：成员按 utf32→utf8 无损转码（get_kv 解码）后经 OsStrExt::from_bytes 喂 Command。

use std::ffi::OsStr;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::process::ExitStatusExt;
use std::process::Command;

use crate::engine::Engine;
use crate::ffi::*;

const SEP: &str = "·";

pub fn exec(eng: &Engine, pc: &str) {
    let args = read_str_list(eng, &read_path(eng, pc, 0));
    let envs = read_str_list(eng, &read_path(eng, pc, 1));
    let code = run(&args, &envs);
    eng.set_tlv_encoded(&eng.write0(pc), "uint8", &[code], &[]);
}

/// 取读参 idx 的容器 KV 路径（ResolveRead 对数组只返首元素，容器须用 ResolveReadPath）。
fn read_path(eng: &Engine, pc: &str, idx: i32) -> String {
    take(unsafe { kvlang_rwirextResolveReadPath(eng.kv, cs(pc).as_ptr(), idx) })
}

/// stringkeymap 容器路径 p → 按坐标段 ·[i] 数值升序取各成员字符串（get_kv 解 char/utf32）。
fn read_str_list(eng: &Engine, path: &str) -> Vec<String> {
    if path.is_empty() {
        return Vec::new();
    }
    let mut idxs: Vec<usize> = eng
        .list_kv(&format!("{path}{SEP}"))
        .iter()
        .filter_map(|n| n.trim_start_matches('[').trim_end_matches(']').parse().ok())
        .collect();
    idxs.sort_unstable();
    idxs.iter()
        .map(|i| eng.get_kv(&format!("{path}{SEP}[{i}]")))
        .collect()
}

fn run(args: &[String], envs: &[String]) -> u8 {
    let Some(prog) = args.first() else {
        return 127;
    };
    let mut cmd = Command::new(OsStr::from_bytes(prog.as_bytes()));
    for a in &args[1..] {
        cmd.arg(OsStr::from_bytes(a.as_bytes()));
    }
    if !envs.is_empty() {
        cmd.env_clear();
        for e in envs {
            if let Some((k, v)) = e.split_once('=') {
                cmd.env(
                    OsStr::from_bytes(k.as_bytes()),
                    OsStr::from_bytes(v.as_bytes()),
                );
            }
        }
    }
    match cmd.status() {
        Ok(st) => st
            .code()
            .map(|c| c as u8)
            .unwrap_or_else(|| (128 + st.signal().unwrap_or(0)) as u8),
        Err(_) => 127,
    }
}
