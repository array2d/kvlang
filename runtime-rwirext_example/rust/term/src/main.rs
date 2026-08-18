//! term 扩展 runtime：模式2（runtime 主导 + term 嵌入，单线程函数调用）。
//! 专注一个 vthread 的 print/println/cerr：bootstrap 拿 vid 后循环
//!   execute_vthread(vid)（runtime 主导执行，遇 ext rwir 直接返回 pc）
//!   → RunSeq 连续处理己方 print → 写回 vthread pc → 继续，
//! 直到 vthread done，term 退出进程。

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::io::Write;
use std::ptr::null_mut;

#[repr(C)]
struct kvlang_rt {
    _p: [u8; 0],
}

// 对齐 C 的 struct rwext_conn { kv_t *kv; }（单指针）。
#[repr(C)]
struct rwext_conn {
    kv: *mut c_void,
}

unsafe impl Send for kvlang_rt {}
unsafe impl Send for rwext_conn {}

unsafe extern "C" {
    // kvlang_rt ABI（主导执行）
    fn kvlang_rt_connect(dsn: *const c_char) -> *mut kvlang_rt;
    fn kvlang_rt_kv(rt: *mut kvlang_rt) -> *mut c_void;
    fn kvlang_rt_bootstrap(rt: *mut kvlang_rt, funcname: *const c_char,
                           args: *const *const c_char, nargs: c_int) -> *mut c_char;
    fn kvlang_rt_execute_vthread(rt: *mut kvlang_rt, vid: *const c_char, out_pc: *mut *mut c_char) -> c_int;

    // rwext ABI（注册 / 处理 print）
    fn rwext_register(c: *mut rwext_conn, opcode: *const c_char, nr: c_int, nw: c_int, sig: *const c_char) -> c_int;
    fn rwext_set(c: *mut rwext_conn, key: *const c_char, val: *const c_char) -> c_int;
    fn rwext_print_line(c: *mut rwext_conn, pc: *const c_char, rawnl: *mut c_int, cerr: *mut c_int) -> *mut c_char;
    fn rwext_next_pc(pc: *const c_char) -> *mut c_char;
}

struct Op {
    name: &'static str,
    sig: &'static str,
    nr: i32,
    nw: i32,
}

const OPS: &[Op] = &[
    Op { name: "print", sig: "any...", nr: 1, nw: 0 },
    Op { name: "println", sig: "any...", nr: 1, nw: 0 },
    Op { name: "cerr", sig: "any...", nr: 1, nw: 0 },
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

fn register(c: *mut rwext_conn) {
    for op in OPS {
        unsafe {
            rwext_register(c, cs(op.name).as_ptr(), op.nr, op.nw, cs(op.sig).as_ptr());
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
    let funcname = std::env::args().nth(1).unwrap_or_else(|| "main".to_string());

    let rt = unsafe { kvlang_rt_connect(cs(&dsn).as_ptr()) };
    if rt.is_null() {
        eprintln!("term: kvlang_rt_connect failed: {dsn}");
        std::process::exit(1);
    }
    let kv = unsafe { kvlang_rt_kv(rt) };
    let mut conn = rwext_conn { kv };

    register(&mut conn);

    let vid = unsafe { kvlang_rt_bootstrap(rt, cs(&funcname).as_ptr(), null_mut(), 0) };
    if vid.is_null() {
        eprintln!("term: bootstrap {funcname} failed");
        std::process::exit(1);
    }
    let vid = take(vid);
    let vpc = format!("/vthread/{vid}/\u{2025}pc");

    loop {
        // runtime 主导执行，遇 ext rwir 直接返回 pc
        let mut pc: *mut c_char = null_mut();
        let rc = unsafe { kvlang_rt_execute_vthread(rt, cs(&vid).as_ptr(), &mut pc) };
        if rc == 0 {
            break; // vthread done
        }
        if rc != 1 {
            break; // 错误
        }

        // RunSeq：连续处理己方 print，遇非己方停下（c 停在非己方 pc）
        let mut c = take(pc);
        loop {
            let mut rawnl = 0i32;
            let mut is_cerr = 0i32;
            let p = unsafe { rwext_print_line(&mut conn, cs(&c).as_ptr(), &mut rawnl, &mut is_cerr) };
            if p.is_null() {
                break;
            }
            let line = take(p);
            print_line(&line, rawnl, is_cerr);
            c = take(unsafe { rwext_next_pc(cs(&c).as_ptr()) });
        }

        // 写回非己方 pc，让 runtime 从它继续
        unsafe {
            rwext_set(&mut conn, cs(&vpc).as_ptr(), cs(&c).as_ptr());
        }
    }
}
