//! term 扩展 runtime：第一个通过 C ABI（kvlang_rwext）嵌入 C runtime 的扩展。
//! 用 Rust 实现，注册 print/println/cerr，常驻 serve 循环执行外部 rwir。

use std::ffi::{c_char, c_int, CStr, CString};
use std::time::Duration;

#[repr(C)]
struct rwext_conn {
    _p: [u8; 0],
}

unsafe impl Send for rwext_conn {}

unsafe extern "C" {
    fn rwext_connect(dsn: *const c_char) -> *mut rwext_conn;
    fn rwext_disconnect(c: *mut rwext_conn);
    fn rwext_register(c: *mut rwext_conn, opcode: *const c_char, nr: i32, nw: i32, sig: *const c_char) -> c_int;
    fn rwext_list(c: *mut rwext_conn, prefix: *const c_char) -> *mut c_char;
    fn rwext_get(c: *mut rwext_conn, key: *const c_char) -> *mut c_char;
    fn rwext_set(c: *mut rwext_conn, key: *const c_char, val: *const c_char) -> c_int;
    fn rwext_del(c: *mut rwext_conn, key: *const c_char) -> c_int;
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
    Op { name: "print", sig: "rwir print(A:any, ...) -> ()", nr: 1, nw: 0 },
    Op { name: "println", sig: "rwir println(A:any, ...) -> ()", nr: 1, nw: 0 },
    Op { name: "cerr", sig: "rwir cerr(A:any, ...) -> ()", nr: 1, nw: 0 },
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

fn serve_op(c: *mut rwext_conn, op: &Op) {
    let base = format!("/lib/{}", op.name);
    let children = take(unsafe { rwext_list(c, cs(&format!("{base}/")).as_ptr()) });
    for child in children.split('\n') {
        if !child.starts_with(".todo<") || !child.ends_with('>') {
            continue;
        }
        let vid = &child[6..child.len() - 1];
        let todo_key = format!("{base}/{child}");
        let pcid = take(unsafe { rwext_get(c, cs(&todo_key).as_ptr()) });
        let (pc, id) = match pcid.rfind('|') {
            Some(i) => (&pcid[..i], &pcid[i + 1..]),
            None => (pcid.as_str(), ""),
        };

        let mut cur = pc.to_string();
        loop {
            let mut rawnl = 0i32;
            let mut is_cerr = 0i32;
            let p = unsafe { rwext_print_line(c, cs(&cur).as_ptr(), &mut rawnl, &mut is_cerr) };
            if p.is_null() {
                break;
            }
            let line = take(p);
            if is_cerr != 0 {
                eprint!("{line}");
                if rawnl == 0 {
                    eprintln!();
                }
            } else {
                print!("{line}");
                if rawnl == 0 {
                    println!();
                }
            }
            cur = take(unsafe { rwext_next_pc(cs(&cur).as_ptr()) });
        }

        let vt_pc = format!("/vthread/{vid}/\u{2025}pc");
        unsafe {
            rwext_set(c, cs(&vt_pc).as_ptr(), cs(&cur).as_ptr());
            let done_key = format!("{base}/.done<{vid}>");
            rwext_set(c, cs(&done_key).as_ptr(), cs(id).as_ptr());
            rwext_del(c, cs(&todo_key).as_ptr());
        }
    }
}

struct Conn(*mut rwext_conn);
unsafe impl Send for Conn {}

fn serve(conn: Conn) {
    register(conn.0);
    loop {
        for op in OPS {
            serve_op(conn.0, op);
        }
        std::thread::sleep(Duration::from_millis(500));
    }
}

#[no_mangle]
pub extern "C" fn rwext_term_start(dsn: *const c_char) {
    let dsn = if dsn.is_null() {
        "redis://127.0.0.1:6379".to_string()
    } else {
        unsafe { CStr::from_ptr(dsn) }.to_string_lossy().into_owned()
    };
    let c = unsafe { rwext_connect(cs(&dsn).as_ptr()) };
    if c.is_null() {
        return;
    }
    let conn = Conn(c);
    std::thread::spawn(move || serve(conn));
}
