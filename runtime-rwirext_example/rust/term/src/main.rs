#![allow(non_snake_case, non_camel_case_types)]
//! term 扩展 runtime：模式2（runtime 主导 + term 嵌入，单线程函数调用）。
//! 专注一个 vthread 的 print/println/cerr：bootstrap 拿 vid 后循环
//!   execute_vthread(vid)（runtime 主导执行，遇 ext rwir 直接返回 pc）
//!   → RunSeq 连续处理己方 print → 写回 vthread pc → 继续，
//! 直到 vthread done，term 退出进程。
//!
//! KV 存取（连接/读状态/写 pc）扩展宿主自己走 kvspace ABI，不经 runtime；
//! runtime 只提供 kvspace 没有的语义（注册 rwir、resolve+display、PC 推进）。

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::io::Write;
use std::ptr::null_mut;

#[repr(C)]
struct kvlangRuntime_t {
    _p: [u8; 0],
}

// 对齐 kvspace ABI 的 kvspaceHead_t（repr(C)）：kindexpr 为唯一类型真相，body 段定位靠 offset/len。
#[repr(C)]
struct kvspaceHead_t {
    kindexpr: [u8; 256],
    ro: u8,
    vid: u32,
    body_len: i32,
    body_offset: i32,
}

unsafe impl Send for kvlangRuntime_t {}

unsafe extern "C" {
    // kvlangRuntime_t ABI（主导执行）
    fn kvlangRuntimeConnect(dsn: *const c_char) -> *mut kvlangRuntime_t;
    fn kvlangRuntimeBootstrap(
        rt: *mut kvlangRuntime_t,
        funcname: *const c_char,
        args: *const *const c_char,
        nargs: c_int,
    ) -> *mut c_char;
    fn kvlangRuntimeExecuteVthread(
        rt: *mut kvlangRuntime_t,
        vid: *const c_char,
        out_pc: *mut *mut c_char,
    ) -> c_int;

    // kvspace ABI（扩展宿主自连：读状态、写 pc）
    fn kvspaceConnect(dsn: *const c_char) -> *mut c_void;
    fn kvspaceFree(h: *mut c_void);
    fn kvspaceBytesFree(p: *mut u8, len: u32);
    fn kvspaceGet(h: *mut c_void, key: *const c_char, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceSet(
        h: *mut c_void,
        keys: *const *const c_char,
        vals: *const u8,
        lens: *const u32,
        n: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceNewChar(kind: *const c_char, s: *const c_char, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceDecodeHead(data: *const u8, data_len: u32, out: *mut kvspaceHead_t) -> c_int;

    // rwirext ABI（kvspace 不提供的 runtime 语义；句柄传扩展自连的 kvspace）
    fn kvlang_rwirextRegister(
        kvspace: *mut c_void,
        opcode: *const c_char,
        nr: c_int,
        nw: c_int,
        sig: *const c_char,
    ) -> c_int;
    fn kvlang_rwirextHandoff(
        kvspace: *mut c_void,
        vtid: *const c_char,
        pc: *const c_char,
    ) -> c_int;
    fn kvlang_rwirextIsExt(kvspace: *mut c_void, opcode: *const c_char) -> c_int;
    fn kvlang_rwirextParams(kvspace: *mut c_void, pc: *const c_char) -> *mut c_char;
    fn kvlang_rwirextResolveRead(
        kvspace: *mut c_void,
        pc: *const c_char,
        idx: c_int,
    ) -> *mut c_char;
    fn kvlang_rwirextNextPc(pc: *const c_char) -> *mut c_char;
}

struct Op {
    name: &'static str,
    sig: &'static str,
    nr: i32,
    nw: i32,
}

const OPS: &[Op] = &[
    Op {
        name: "print",
        sig: "any...",
        nr: 1,
        nw: 0,
    },
    Op {
        name: "println",
        sig: "any...",
        nr: 1,
        nw: 0,
    },
    Op {
        name: "cerr",
        sig: "any...",
        nr: 1,
        nw: 0,
    },
];

fn cs(s: &str) -> CString {
    CString::new(s).unwrap()
}

fn take(p: *mut c_char) -> String {
    if p.is_null() {
        String::new()
    } else {
        let s = unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned();
        unsafe { libc::free(p as *mut libc::c_void) };
        s
    }
}

// 读 key 的字符串值：kvspaceGet 拿 TLV → decodeHead 定位 body 段（char/utf8 的 body 即 UTF-8 串）。
fn kv_get(kv: *mut c_void, key: &str) -> String {
    let mut out: *mut u8 = null_mut();
    let mut out_len: u32 = 0;
    let rc = unsafe { kvspaceGet(kv, cs(key).as_ptr(), &mut out, &mut out_len) };
    if rc != 0 || out.is_null() || out_len == 0 {
        if !out.is_null() {
            unsafe { kvspaceBytesFree(out, out_len) };
        }
        return String::new();
    }
    let mut h = kvspaceHead_t {
        kindexpr: [0; 256],
        ro: 0,
        vid: 0,
        body_len: 0,
        body_offset: 0,
    };
    let s = if unsafe { kvspaceDecodeHead(out, out_len, &mut h) } == 0
        && h.body_len > 0
        && (h.body_offset as i64 + h.body_len as i64) <= out_len as i64
    {
        let start = h.body_offset as usize;
        let end = start + h.body_len as usize;
        let slice = unsafe { std::slice::from_raw_parts(out, out_len as usize) };
        String::from_utf8_lossy(&slice[start..end]).into_owned()
    } else {
        String::new()
    };
    unsafe { kvspaceBytesFree(out, out_len) };
    s
}

// 写 char/utf8 值：kvspaceNewChar 编 TLV → kvspaceSet 落盘。
fn kv_set(kv: *mut c_void, key: &str, val: &str) {
    let mut out: *mut u8 = null_mut();
    let mut out_len: u32 = 0;
    if unsafe { kvspaceNewChar(cs("char/utf8").as_ptr(), cs(val).as_ptr(), &mut out, &mut out_len) } != 0
        || out.is_null()
    {
        return;
    }
    let key_c = cs(key);
    let keys: [*const c_char; 1] = [key_c.as_ptr()];
    let lens: [u32; 1] = [out_len];
    let mut err = [0 as c_char; 256];
    unsafe {
        kvspaceSet(kv, keys.as_ptr(), out, lens.as_ptr(), 1, err.as_mut_ptr(), 256);
        kvspaceBytesFree(out, out_len);
    }
}

fn register(kv: *mut c_void) {
    for op in OPS {
        unsafe {
            kvlang_rwirextRegister(kv, cs(op.name).as_ptr(), op.nr, op.nw, cs(op.sig).as_ptr());
        }
    }
}

fn print_line(line: &str, rawnl: i32, is_cerr: i32) {
    if is_cerr != 0 {
        eprint!("{line}");
        if rawnl == 0 {
            eprintln!();
        }
        std::io::stderr().flush().ok();
    } else {
        print!("{line}");
        if rawnl == 0 {
            println!();
        }
        std::io::stdout().flush().ok();
    }
}

fn main() {
    let dsn = std::env::var("KVSPACE").unwrap_or_else(|_| "redis://127.0.0.1:6379".to_string());
    let funcname = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "main".to_string());

    let rt = unsafe { kvlangRuntimeConnect(cs(&dsn).as_ptr()) };
    if rt.is_null() {
        eprintln!("kvlang: kvlangRuntimeConnect failed: {dsn}");
        std::process::exit(1);
    }
    let kv = unsafe { kvspaceConnect(cs(&dsn).as_ptr()) };
    if kv.is_null() {
        eprintln!("kvlang: kvspaceConnect failed: {dsn}");
        std::process::exit(1);
    }

    register(kv);

    let vid = unsafe { kvlangRuntimeBootstrap(rt, cs(&funcname).as_ptr(), null_mut(), 0) };
    if vid.is_null() {
        eprintln!("kvlang: bootstrap {funcname} failed");
        std::process::exit(1);
    }
    let vid = take(vid);
    let vpc = format!("/vthread/{vid}/\u{2025}pc");

    loop {
        // runtime 主导执行，遇 ext rwir 直接返回 pc
        let mut pc: *mut c_char = null_mut();
        let rc = unsafe { kvlangRuntimeExecuteVthread(rt, cs(&vid).as_ptr(), &mut pc) };
        if rc == 0 {
            break; // vthread done
        }
        if rc != 1 {
            // 错误：runtime 已把 status/错误信息写进 vthread，读出来上报，别静默吞掉整帧。
            let st = kv_get(kv, &format!("/vthread/{vid}/\u{2025}status"));
            let msg = kv_get(kv, &format!("/vthread/{vid}/\u{2025}error/msg"));
            let st = if st.is_empty() {
                "error".to_string()
            } else {
                st
            };
            eprintln!("kvlang: vthread {vid} {st}: {msg}");
            std::process::exit(1);
        }

        // RunSeq：连续处理己方 print/println/cerr，遇非己方指令停下（c 停在非己方 pc）
        let mut c = take(pc);
        let non_print = loop {
            let params = take(unsafe { kvlang_rwirextParams(kv, cs(&c).as_ptr()) });
            let mut it = params.split('\n');
            let opcode = it.next().unwrap_or("");
            let reads: Vec<&str> = it.collect();
            let (sep, rawnl, is_cerr) = match opcode {
                "print" => ("", 1, 0),
                "println" => (" ", 0, 0),
                "cerr" => (" ", 0, 1),
                _ => break opcode.to_string(),
            };
            let nr = reads.len();
            let mut line = String::new();
            for i in 0..nr {
                if i > 0 {
                    line.push_str(sep);
                }
                let d = take(unsafe { kvlang_rwirextResolveRead(kv, cs(&c).as_ptr(), i as c_int) });
                line.push_str(&d);
            }
            print_line(&line, rawnl, is_cerr);
            c = take(unsafe { kvlang_rwirextNextPc(cs(&c).as_ptr()) });
        };

        // 外部扩展 rwir（json.to/numpy…）：handoff 给对应扩展进程，扩展写回下一 PC 并 signal .done
        if !non_print.is_empty() && unsafe { kvlang_rwirextIsExt(kv, cs(&non_print).as_ptr()) } != 0 {
            if unsafe { kvlang_rwirextHandoff(kv, cs(&vid).as_ptr(), cs(&c).as_ptr()) } != 0 {
                eprintln!("kvlang: handoff {non_print} failed at {c}");
                std::process::exit(1);
            }
        } else {
            // native/control/帧结束：写回 pc 让 runtime 继续/判 done
            kv_set(kv, &vpc, &c);
        }
    }

    unsafe { kvspaceFree(kv) };
}
