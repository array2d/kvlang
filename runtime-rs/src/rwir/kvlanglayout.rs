//! rwir `kvlang·*`：自造 kv 代码入库/导出（直连 layout 的 C ABI）。
//!   kvlang·vet(src)     -> "ok" | 错误信息      只校验（parse+lower），不写 kvspace
//!   kvlang·format(src)  -> 规范化源码 | "error: …"  格式化（parse→规范化），不写 kvspace
//!   kvlang·layout(src)  -> entry | "error: …"   把内存源码 layout 进 kvspace
//!   kvlang·printlib(lib) -> 源码 | "error: …"  把 /lib 子树重建为可运行 kvlang（不读 .src）
//!   kvlang·printstack(vid)-> 文本 | "error: …"  把 /vthread/<vid> 活动栈渲染为文本
//! 五者都在 C 边界 catch_unwind：坏代码返回 -1，绝不打崩宿主进程。

use std::ffi::c_char;

use crate::engine::Engine;
use crate::ffi::*;

fn buf() -> [u8; 4096] {
    [0u8; 4096]
}

pub fn vet(_eng: &Engine, src: &str) -> String {
    let mut err = buf();
    let rc = unsafe {
        kvlangLayoutVet(
            cs(src).as_ptr(),
            err.as_mut_ptr() as *mut c_char,
            err.len() as u32,
        )
    };
    if rc == 0 {
        "ok".to_string()
    } else {
        cbuf(&err)
    }
}

pub fn format(_eng: &Engine, src: &str) -> String {
    let mut out = vec![0u8; 65536];
    let mut err = buf();
    let rc = unsafe {
        kvlangLayoutFormat(
            cs(src).as_ptr(),
            out.as_mut_ptr() as *mut c_char,
            out.len() as u32,
            err.as_mut_ptr() as *mut c_char,
            err.len() as u32,
        )
    };
    if rc == 0 {
        cbuf(&out)
    } else {
        format!("error: {}", cbuf(&err))
    }
}

pub fn layout(eng: &Engine, code: &str) -> String {
    let (mut entry, mut err) = (buf(), buf());
    let rc = unsafe {
        kvlangLayoutCode(
            cs(code).as_ptr(),
            cs(&eng.dsn).as_ptr(),
            entry.as_mut_ptr() as *mut c_char,
            entry.len() as u32,
            err.as_mut_ptr() as *mut c_char,
            err.len() as u32,
        )
    };
    if rc == 0 {
        cbuf(&entry)
    } else {
        format!("error: {}", cbuf(&err))
    }
}

pub fn printlib(eng: &Engine, lib: &str) -> String {
    let mut out = vec![0u8; 65536];
    let mut err = buf();
    let rc = unsafe {
        kvlangLayoutPrintlib(
            cs(lib).as_ptr(),
            cs(&eng.dsn).as_ptr(),
            out.as_mut_ptr() as *mut c_char,
            out.len() as u32,
            err.as_mut_ptr() as *mut c_char,
            err.len() as u32,
        )
    };
    if rc == 0 {
        cbuf(&out)
    } else {
        format!("error: {}", cbuf(&err))
    }
}

/// printstack(vid)：把 /vthread/<vid> 的活动栈渲染成文本。
/// vid 空串 = 当前 vthread —— 从本 rwir 自己的 PC 前缀 `/vthread/<vid>/…` 取。
pub fn printstack(eng: &Engine, pc: &str, vid: &str) -> String {
    let vid = if vid.is_empty() {
        current_vid(pc)
    } else {
        vid.to_string()
    };
    if vid.is_empty() {
        return "error: printstack 无法确定 vthread（传 vid 或从 vthread 内调用）".to_string();
    }
    let mut out = vec![0u8; 65536];
    let mut err = buf();
    let rc = unsafe {
        kvlangLayoutPrintstack(
            cs(&vid).as_ptr(),
            cs(&eng.dsn).as_ptr(),
            out.as_mut_ptr() as *mut c_char,
            out.len() as u32,
            err.as_mut_ptr() as *mut c_char,
            err.len() as u32,
        )
    };
    if rc == 0 {
        cbuf(&out)
    } else {
        format!("error: {}", cbuf(&err))
    }
}

/// 从 PC 路径取 vid：`/vthread/<vid>/…` → `<vid>`（不是 vthread 内则空串）。
fn current_vid(pc: &str) -> String {
    let rest = pc.strip_prefix("/vthread/").unwrap_or("");
    rest.split('/').next().unwrap_or("").to_string()
}
