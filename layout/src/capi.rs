//! layout 的 C ABI：供第三方（Rust/Python/C 等）把 .kv 代码 layout 进 kvspace，
//! 无需 fork 子进程。符号在 cdylib（libkvlanglayout.so）中导出。
//!
//! 四个入口：
//!   kvlangLayoutVet(src,…)       只校验（parse+lower），不写 kvspace —— 自造代码闸门
//!   kvlangLayoutFormat(src,…)    格式化（parse → 规范化源码），不写 kvspace
//!   kvlangLayoutCode(src,dsn,…)  从源码串 layout 进 kvspace（LLM 生成即插入，不落盘）
//!   kvlangLayoutPrintlib(lib,dsn,…)  把 /lib 子树逆向重建为可运行 kvlang（审查 layout 结果，不读 .src）
//!   kvlangLayoutPrintstack(vid,dsn,…) 把 /vthread 活动栈渲染成可读文本（帧链 + 实参 + 当前指令）
//! kvlangLayoutFile(path,…) 是 Code 的薄封装（读文件后走同一 core）。源码读回（`.src`）
//! 是纯 KV 读（/lib/<fn>.src），不在此 ABI。
//!
//! 各入口都在 C 边界用 catch_unwind 兜住 kvlang 内部 panic（设计上对非法输入 panic），
//! 坏代码只会返回 -1，绝不打崩宿主进程。

use std::ffi::CStr;
use std::fs;
use std::os::raw::c_char;
use std::panic::catch_unwind;

use crate::{compile, format, init_dirs, kvkind, printlib, printstack, vet, Kv};

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

/// 把源码 layout 进 dsn 指向的 kvspace。entry_out = 本次写入的 init 函数名列表（\n 分隔，
/// pkg 严格取自 `lib` 声明）：`lib X {…}` → `X·init`，裸顶层语句 → `init`，无 init → 空串。
/// 消费方据此按 lib 声明驱动 init（tutorial 仍按约定跑 test；`\S+` 匹配者取首个入口）。
fn layout_core(src: &str, dsn: &str) -> Result<String, String> {
    let mut kv = Kv::conn(dsn);
    init_dirs(&mut kv)?;
    Ok(compile(&mut kv, src)?.join("\n"))
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

/// printlib：把 lib 前缀下的整棵子树重构为可运行的 kvlang 源码（还原 `lib {}` 与 `rwfunc`），
/// lower 后的原始槽位以 `#` 注释附在各自函数后，供审查。
/// **只看 layout 结果**：签名读参数定义键、体读线性指令槽，不读 `.src` 源码副本。
/// 成功返回 0（out=printlib 文本），失败返回 -1（err_out=错误）。
#[no_mangle]
pub extern "C" fn kvlangLayoutPrintlib(
    lib: *const c_char,
    dsn: *const c_char,
    out: *mut c_char,
    out_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let lib = cstr(lib).to_string();
    let dsn = cstr(dsn).to_string();
    let r = catch_unwind(|| -> Result<String, String> {
        let mut kv = Kv::conn(&dsn);
        Ok(printlib(&mut kv, &lib))
    });
    match r {
        Ok(Ok(s)) => {
            write_out(out, out_cap, &s);
            0
        }
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "printlib panicked");
            -1
        }
    }
}

/// printstack：把 /vthread/<vid> 的活动栈渲染成可读文本（帧链 + 每帧实参 + 顶帧当前指令）。
/// 只读，不改 ‥pc/‥status；暂停/恢复由调用方（harness）负责。
/// 成功返回 0（out=printstack 文本），失败返回 -1（err_out=错误）。
#[no_mangle]
pub extern "C" fn kvlangLayoutPrintstack(
    vid: *const c_char,
    dsn: *const c_char,
    out: *mut c_char,
    out_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let vid = cstr(vid).to_string();
    let dsn = cstr(dsn).to_string();
    let r = catch_unwind(|| -> Result<String, String> {
        let mut kv = Kv::conn(&dsn);
        Ok(printstack(&mut kv, &vid))
    });
    match r {
        Ok(Ok(s)) => {
            write_out(out, out_cap, &s);
            0
        }
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "printstack panicked");
            -1
        }
    }
}

// ── langtype 解析 ABI ──────────────────────────────────────────────────
// langtype 语法唯一事实源在 layout（langtype.rs）；解析能力导出为 C ABI，
// 供 runtime 之外的消费方（扩展宿主 term/numpy/json、byteseek…）读取 XValue head 时
// 复用，杜绝各处手写 head 结构/解析造成的 ABI 漂移（#70 遗留的旧 kind[32] 结构即此类）。

/// langtype 解析结果（repr(C)，内存布局 = i32,i32,[i32;8],i32,[u8;64]）。
#[repr(C)]
pub struct kvlangKindexpr {
    pub ref_: i32,      // 0=内联 1=指针(*) 2=扩展句柄(@)
    pub ndim: i32,      // 维数（0=标量）
    pub dims: [i32; 8], // 各维大小（前 ndim 项有效）
    pub array_len: i32, // 元素总数（标量=1，多维=各维乘积）
    pub kind: [u8; 64], // NUL-terminated type.
}

/// 解析 XValue head 的 langtype 内容（NUL 终止串，含 */@ 前缀与 [dims]）。
/// Returns 0 on success, or -1 without modifying `out` on null/empty/non-UTF-8 input,
/// `ndim > 8`, negative dims, `i32` product overflow, or `kind` > 63 bytes. Catches panics across FFI.
///
/// # Safety
/// `langtype` must be NULL or NUL-terminated; `out` must be NULL or point to a writable `kvlangKindexpr`.
#[no_mangle]
pub unsafe extern "C" fn kvlangKindexprParse(
    langtype: *const c_char,
    out: *mut kvlangKindexpr,
) -> i32 {
    if langtype.is_null() || out.is_null() {
        return -1;
    }
    let s = cstr(langtype);
    match catch_unwind(|| parse_kindexpr(s)) {
        Ok(Some(v)) => {
            // SAFETY: caller guarantees `out` points to a writable `kvlangKindexpr`.
            unsafe { out.write(v) };
            0
        }
        _ => -1,
    }
}

/// Pure Rust core of [`kvlangKindexprParse`]: `*`/`@` sets `ref_` (1 ptr / 2 ext), followed by `[dims]kind`.
fn parse_kindexpr(s: &str) -> Option<kvlangKindexpr> {
    let (ref_, body) = match s.as_bytes().first()? {
        b'*' => (1, &s[1..]),
        b'@' => (2, &s[1..]),
        _ => (0, s),
    };
    if body.is_empty() {
        return None;
    }
    let (dims, kind) = kvkind::parse_langtype(body);
    let mut out = kvlangKindexpr {
        ref_,
        ndim: 0,
        dims: [0; 8],
        array_len: 1,
        kind: [0; 64],
    };
    if dims.len() > out.dims.len() {
        return None;
    }
    out.ndim = dims.len() as i32;
    for (i, &d) in dims.iter().enumerate() {
        if d < 0 {
            return None;
        }
        out.dims[i] = d;
        out.array_len = out.array_len.checked_mul(d)?;
    }
    let kb = kind.as_bytes();
    if kb.len() >= out.kind.len() {
        return None;
    }
    out.kind[..kb.len()].copy_from_slice(kb);
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(s: &str) -> Option<kvlangKindexpr> {
        let c = std::ffi::CString::new(s).unwrap();
        let mut out = kvlangKindexpr {
            ref_: -9,
            ndim: -9,
            dims: [-9; 8],
            array_len: -9,
            kind: [0xff; 64],
        };
        let rc = unsafe { kvlangKindexprParse(c.as_ptr(), &mut out) };
        (rc == 0).then_some(out)
    }

    fn kind(k: &kvlangKindexpr) -> &str {
        let n = k.kind.iter().position(|&b| b == 0).unwrap();
        std::str::from_utf8(&k.kind[..n]).unwrap()
    }

    #[test]
    fn kindexpr_parse_contract() {
        let k = parse("[2,3]int64").unwrap();
        assert_eq!((k.ref_, k.ndim, &k.dims[..2], k.array_len, kind(&k)), (0, 2, &[2, 3][..], 6, "int64"));
        let k = parse("*[4]float32").unwrap();
        assert_eq!((k.ref_, k.ndim, k.array_len, kind(&k)), (1, 1, 4, "float32"));
        let k = parse("@char/utf8").unwrap();
        assert_eq!((k.ref_, k.ndim, k.array_len, kind(&k)), (2, 0, 1, "char/utf8"));
        // Reject ndim > 8, i32 overflow, negative dims, overlong kind, and empty input.
        assert!(parse("[1,1,1,1,1,1,1,1,1]int64").is_none());
        assert!(parse("[65536,65536]int64").is_none());
        assert!(parse("[-1]int64").is_none());
        assert!(parse(&format!("/lib/{}", "x".repeat(70))).is_none());
        assert!(parse("").is_none() && parse("*").is_none());
        let rc = unsafe { kvlangKindexprParse(std::ptr::null(), std::ptr::null_mut()) };
        assert_eq!(rc, -1);
    }
}
