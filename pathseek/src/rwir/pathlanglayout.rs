
use std::ffi::c_char;

use crate::engine::Engine;
use crate::ffi::*;

fn buf() -> [u8; 4096] {
    [0u8; 4096]
}

pub fn vet(_eng: &Engine, src: &str) -> String {
    let mut err = buf();
    let rc = unsafe {
        pathlangLayoutVet(
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
    let mut out = vec![0u8; src.len() * 4 + 65536];
    let mut err = buf();
    let rc = unsafe {
        pathlangLayoutFormat(
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
        pathlangLayoutCode(
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
    let mut out = vec![0u8; 524288];
    let mut err = buf();
    let rc = unsafe {
        pathlangLayoutPrintlib(
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

pub fn printstack(eng: &Engine, pc: &str, vid: &str) -> String {
    let vid = if vid.is_empty() {
        current_vid(pc)
    } else {
        vid.to_string()
    };
    if vid.is_empty() {
        return "error: printstack cannot determine vthread (pass vid or call from inside a vthread)".to_string();
    }
    let mut out = vec![0u8; 262144];
    let mut err = buf();
    let rc = unsafe {
        pathlangLayoutPrintstack(
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

fn current_vid(pc: &str) -> String {
    let rest = pc.strip_prefix("/vthread/").unwrap_or("");
    rest.split('/').next().unwrap_or("").to_string()
}
