//! layout 的 C ABI：供第三方（Rust/Python/C 等）把 .kv 代码 layout 进 kvspace，
//! 无需 fork 子进程。符号在 cdylib（libkvlang_layout.so）中导出。
//!
//! 三个入口：
//!   kvlangLayoutVet(src,…)       只校验（parse+lower），不写 kvspace —— 自造代码闸门
//!   kvlangLayoutFormat(src,…)    格式化（parse → 规范化源码），不写 kvspace
//!   kvlangLayoutCode(src,dsn,…)  从源码串 layout 进 kvspace（LLM 生成即插入，不落盘）
//! kvlangLayoutFile(path,…) 是 Code 的薄封装（读文件后走同一 core）。源码读回（`.src`）
//! 是纯 KV 读（/lib/<fn>.src），不在此 ABI。
//!
//! 各入口都在 C 边界用 catch_unwind 兜住 kvlang 内部 panic（设计上对非法输入 panic），
//! 坏代码只会返回 -1，绝不打崩宿主进程。

use std::ffi::CStr;
use std::fs;
use std::os::raw::c_char;
use std::panic::catch_unwind;

use crate::{compile, format, init_dirs, vet, Kv};

/// 复刻 Go runtime / layout_file 的 findEntry：DFS /lib/ 找首个 `.init`，否则 "init"。
fn find_entry(kv: &mut Kv, prefix: &str) -> String {
    for c in kv.list(prefix, false, true) {
        let c = c.trim_end_matches('/');
        if c.ends_with(".init") {
            return c.to_string();
        }
        let sub = format!("{prefix}{c}/");
        let entry = find_entry(kv, &sub);
        if !entry.is_empty() {
            return format!("{c}/{entry}");
        }
    }
    String::new()
}

fn cstr<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(p).to_str().unwrap_or("") }
}

fn write_out(buf: *mut c_char, cap: u32, s: &str) {
    if buf.is_null() || cap == 0 {
        return;
    }
    let b = s.as_bytes();
    let n = b.len().min(cap as usize - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(b.as_ptr(), buf as *mut u8, n);
        *buf.add(n) = 0;
    }
}

/// 把源码 layout 进 dsn 指向的 kvspace，返回入口名或错误。
fn layout_core(src: &str, dsn: &str) -> Result<String, String> {
    let mut kv = Kv::conn(dsn);
    init_dirs(&mut kv)?;
    compile(&mut kv, src)?;
    let entry = find_entry(&mut kv, "/lib/");
    Ok(if entry.is_empty() { "init".to_string() } else { entry })
}

/// 结果落地为 C 约定：成功写 entry、返回 0；失败写 err、返回 -1；panic 兜为 -1。
fn finish(
    r: std::thread::Result<Result<String, String>>,
    entry_out: *mut c_char,
    entry_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    match r {
        Ok(Ok(entry)) => {
            write_out(entry_out, entry_cap, &entry);
            0
        }
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "layout panicked (invalid program)");
            -1
        }
    }
}

/// 读 `path` 指向的 .kv 文件，layout 进 `dsn`。成功返回 0（entry_out=入口名），失败返回 -1。
#[no_mangle]
pub extern "C" fn kvlangLayoutFile(
    path: *const c_char,
    dsn: *const c_char,
    entry_out: *mut c_char,
    entry_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let (path, dsn) = (cstr(path).to_string(), cstr(dsn).to_string());
    let r = catch_unwind(|| {
        let src = fs::read_to_string(&path).map_err(|e| format!("read {path}: {e}"))?;
        layout_core(&src, &dsn)
    });
    finish(r, entry_out, entry_cap, err_out, err_cap)
}

/// 把内存源码串 `src` 直接 layout 进 `dsn`（LLM 生成即插入，不落盘）。
/// 成功返回 0（entry_out=入口名），失败返回 -1（err_out=错误）。
#[no_mangle]
pub extern "C" fn kvlangLayoutCode(
    src: *const c_char,
    dsn: *const c_char,
    entry_out: *mut c_char,
    entry_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let (src, dsn) = (cstr(src).to_string(), cstr(dsn).to_string());
    let r = catch_unwind(|| layout_core(&src, &dsn));
    finish(r, entry_out, entry_cap, err_out, err_cap)
}

/// 只校验 `src`（parse+lower），不写 kvspace。合法返回 0，非法返回 -1（err_out=错误）。
#[no_mangle]
pub extern "C" fn kvlangLayoutVet(src: *const c_char, err_out: *mut c_char, err_cap: u32) -> i32 {
    let src = cstr(src).to_string();
    match catch_unwind(|| vet(&src)) {
        Ok(Ok(())) => 0,
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "vet panicked (invalid program)");
            -1
        }
    }
}

/// 格式化 `src`（parse → 规范化源码），不写 kvspace。合法返回 0（out=格式化结果），
/// 非法返回 -1（err_out=错误）。
#[no_mangle]
pub extern "C" fn kvlangLayoutFormat(
    src: *const c_char,
    out: *mut c_char,
    out_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let src = cstr(src).to_string();
    match catch_unwind(|| format(&src)) {
        Ok(Ok(s)) => {
            write_out(out, out_cap, &s);
            0
        }
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "format panicked (invalid program)");
            -1
        }
    }
}
