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

use crate::{compile, format, init_dirs, kvkind, vet, Kv};

/// 复刻 Go runtime / layout_file 的 findEntry：DFS /lib/ 找首个 `·init`，否则 "init"。
fn find_entry(kv: &mut Kv, prefix: &str) -> String {
    for c in kv.list(prefix, false, true) {
        let c = c.trim_end_matches('/');
        if c.ends_with("·init") {
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

// ── kindexpr 解析 ABI ──────────────────────────────────────────────────
// kindexpr 语法唯一事实源在 layout（kindexpr.rs）；解析能力导出为 C ABI，
// 供 runtime 之外的消费方（扩展宿主 term/numpy/json、byteseek…）读取 XValue head 时
// 复用，杜绝各处手写 head 结构/解析造成的 ABI 漂移（#70 遗留的旧 kind[32] 结构即此类）。

/// kindexpr 解析结果（repr(C)，内存布局 = i32,i32,[i32;8],i32,[u8;64]）。
#[repr(C)]
pub struct kvlangKindexpr {
    pub ref_: i32,      // 0=内联 1=软链接(*) 2=扩展句柄(@)
    pub ndim: i32,      // 维数（0=标量）
    pub dims: [i32; 8], // 各维大小（前 ndim 项有效）
    pub array_len: i32, // 元素总数（标量=1，多维=各维乘积）
    pub kind: [u8; 64], // base kind，NUL 终止（如 "float64"、"char/utf8"、"rwir|rwfunc"）
}

/// 解析 XValue head 的 kindexpr 内容（NUL 终止串，含 */@ 前缀与 [dims]）。
/// 成功返回 0，失败（空指针/空串）返回 -1。
#[no_mangle]
pub extern "C" fn kvlangKindexprParse(kindexpr: *const c_char, out: *mut kvlangKindexpr) -> i32 {
    if kindexpr.is_null() || out.is_null() {
        return -1;
    }
    let s = cstr(kindexpr);
    if s.is_empty() {
        return -1;
    }
    let (r, dims, kind) = kvkind::parse_kindexpr(s);
    let out = unsafe { &mut *out };
    out.ref_ = r;
    out.ndim = dims.len() as i32;
    for (i, d) in dims.iter().enumerate() {
        if i < 8 {
            out.dims[i] = *d;
        }
    }
    out.array_len = if dims.is_empty() { 1 } else { dims.iter().product() };
    let kb = kind.as_bytes();
    let n = kb.len().min(63);
    out.kind[..n].copy_from_slice(&kb[..n]);
    out.kind[n] = 0;
    0
}
