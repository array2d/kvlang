//! lib `networld/proc` —— 外部进程。rwir `networld/proc·exec`：同步执行外部命令到结束。
//!   networld/proc·exec(args, envs) -> exitcode, stdout, stderr
//! args/envs 均为 stringkeymap（成员 ·[i] 为 []char/utf32）：args 首成员=可执行文件、
//! 其余为参数；envs 每成员一条 "K=V"，空 = 继承父进程环境、非空 = 完整替换（execve 语义）。
//! exitcode：uint8。正常退出=退出码(0-255)；被信号终止=128+signo；spawn 失败/无 argv=127。
//! stdout/stderr 为 @[]uint8 扩展句柄：仅当调用点绑定该写槽时才捕获——绑定→管道捕获+落
//! 物理文件 /tmp/kvlangruntime-rs/{pid}/{stdout,stderr}+写 @ 句柄（body=逻辑
//! /networld/{host}/proc/{pid}/{stream}）；未绑定→继承父终端（保持无捕获时的直通输出）。

use std::ffi::OsStr;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::process::ExitStatusExt;
use std::process::{Command, Stdio};

use crate::engine::Engine;
use crate::ffi::*;

const SEP: &str = "·";

pub fn exec(eng: &Engine, pc: &str) {
    let args = read_str_list(eng, &read_path(eng, pc, 0));
    let envs = read_str_list(eng, &read_path(eng, pc, 1));
    let out_slot = eng.write_at(pc, 1);
    let err_slot = eng.write_at(pc, 2);
    let r = run(&args, &envs, !out_slot.is_empty(), !err_slot.is_empty());
    eng.set_tlv_encoded(&eng.write0(pc), "uint8", &[r.code], &[]);
    if !out_slot.is_empty() {
        write_handle(eng, &out_slot, r.pid, "stdout", &r.out);
    }
    if !err_slot.is_empty() {
        write_handle(eng, &err_slot, r.pid, "stderr", &r.err);
    }
}

struct Run {
    code: u8,
    pid: u32,
    out: Vec<u8>,
    err: Vec<u8>,
}

fn run(args: &[String], envs: &[String], cap_out: bool, cap_err: bool) -> Run {
    let Some(prog) = args.first() else {
        return Run {
            code: 127,
            pid: 0,
            out: Vec::new(),
            err: Vec::new(),
        };
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
    cmd.stdout(if cap_out {
        Stdio::piped()
    } else {
        Stdio::inherit()
    });
    cmd.stderr(if cap_err {
        Stdio::piped()
    } else {
        Stdio::inherit()
    });
    let child = match cmd.spawn() {
        Ok(c) => c,
        Err(_) => {
            return Run {
                code: 127,
                pid: 0,
                out: Vec::new(),
                err: Vec::new(),
            }
        }
    };
    let pid = child.id();
    match child.wait_with_output() {
        Ok(o) => {
            let code = o
                .status
                .code()
                .map(|c| c as u8)
                .unwrap_or_else(|| (128 + o.status.signal().unwrap_or(0)) as u8);
            Run {
                code,
                pid,
                out: o.stdout,
                err: o.stderr,
            }
        }
        Err(_) => Run {
            code: 127,
            pid,
            out: Vec::new(),
            err: Vec::new(),
        },
    }
}

/// 捕获字节落物理文件 + 该写槽写 @[]uint8 句柄（body=逻辑 /networld/{host}/proc/{pid}/{stream}）。
fn write_handle(eng: &Engine, slot: &str, pid: u32, stream: &str, bytes: &[u8]) {
    let dir = phys_dir(pid);
    std::fs::create_dir_all(&dir).ok();
    std::fs::write(format!("{dir}/{stream}"), bytes).ok();
    let locator = format!("/networld/{}/proc/{pid}/{stream}", super::hostname());
    eng.set_ext_handle(slot, "[]uint8", &locator);
}

fn phys_dir(pid: u32) -> String {
    format!("/tmp/kvlangruntime-rs/{pid}")
}

/// @ 句柄兑现（read_at 调）：逻辑 /networld/{host}/proc/{pid}/{stream} → 物理文件读回字节。
pub fn resolve_read(locator: &str) -> Vec<u8> {
    let parts: Vec<&str> = locator.trim_start_matches('/').split('/').collect();
    if parts.len() == 5 && parts[0] == "networld" && parts[2] == "proc" {
        return std::fs::read(format!("{}/{}", phys_dir_str(parts[3]), parts[4]))
            .unwrap_or_default();
    }
    Vec::new()
}

fn phys_dir_str(pid: &str) -> String {
    format!("/tmp/kvlangruntime-rs/{pid}")
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
