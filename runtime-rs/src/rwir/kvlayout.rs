//! rwir `kvlanglayout·*`：自造 kv 代码入库/导出（直连 layout 的 C ABI）。
//!   kvlanglayout·vet(src)     -> "ok" | 错误信息      只校验（parse+lower），不写 kvspace
//!   kvlanglayout·format(src)  -> 规范化源码 | "error: …"  格式化（parse→规范化），不写 kvspace
//!   kvlanglayout·layout(src)  -> entry | "error: …"   把内存源码 layout 进 kvspace
//!   kvlanglayout·dump(lib)    -> dump 文本 | "error: …"  把 /lib 子树重构为可运行 kvlang 源码
//! 四者都在 C 边界 catch_unwind：坏代码返回 -1，绝不打崩宿主进程。

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

pub fn dump(eng: &Engine, lib: &str) -> String {
    let mut out = vec![0u8; 65536];
    let mut err = buf();
    let rc = unsafe {
        kvlangLayoutDump(
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
