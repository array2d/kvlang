//! layout 的 C ABI：供第三方（Python/C 等）直接把 .kv 代码 layout 进 kvspace，
//! 无需 fork layout_file 子进程。符号在 cdylib（libkvlang_layout.so）中导出。

use std::ffi::CStr;
use std::fs;
use std::os::raw::c_char;

use crate::{compile, init_dirs, Kv};

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

/// 读 `path` 指向的 .kv 文件，layout 进 `dsn` 指定的 kvspace。
/// 成功返回 0，入口名写入 `entry_out`（含 NUL，长度 ≤ entry_cap）；
/// 失败返回 -1，错误信息写入 `err_out`（含 NUL，长度 ≤ err_cap）。
#[no_mangle]
pub extern "C" fn kvlang_layout_file(
    path: *const c_char,
    dsn: *const c_char,
    entry_out: *mut c_char,
    entry_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let src = match fs::read_to_string(cstr(path)) {
        Ok(s) => s,
        Err(e) => {
            write_out(err_out, err_cap, &format!("read {}: {e}", cstr(path)));
            return -1;
        }
    };
    let mut kv = Kv::conn(cstr(dsn));
    if let Err(e) = init_dirs(&mut kv) {
        write_out(err_out, err_cap, &format!("init_dirs: {e}"));
        return -1;
    }
    if let Err(e) = compile(&mut kv, &src) {
        write_out(err_out, err_cap, &e);
        return -1;
    }
    let entry = find_entry(&mut kv, "/lib/");
    write_out(entry_out, entry_cap, if entry.is_empty() { "init" } else { &entry });
    0
}
