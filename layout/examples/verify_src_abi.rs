//! 验证 corebrain 自造代码三入口：kvlangLayoutVet / kvlangLayoutCode + `.src` 读回。
//! 用法：verify_src_abi [dsn]

use std::os::raw::c_char;

use kvlang_layout::capi::{kvlangLayoutCode, kvlangLayoutVet};
use kvlang_layout::Kv;

fn cs(s: &str) -> std::ffi::CString {
    std::ffi::CString::new(s).unwrap()
}

fn main() {
    let dsn = std::env::args().nth(1).unwrap_or_else(|| "redis://127.0.0.1:6379".into());

    let good = "rwfunc addup(a:int64, b:int64) -> c:int64 {\n\tadd(a, b) -> c\n}\n";
    let bad = "rwfunc broken( -> {\n\tthis is not kvlang\n";

    // 1) vet 合法源码 → 0
    let mut err = [0u8; 512];
    let rc = kvlangLayoutVet(cs(good).as_ptr(), err.as_mut_ptr() as *mut c_char, 512);
    println!("vet(good)  rc={rc}  (期望 0)");
    assert_eq!(rc, 0, "合法源码 vet 应通过");

    // 2) vet 非法源码 → -1，且不 panic（catch_unwind 兜住）
    let mut err2 = [0u8; 512];
    let rc = kvlangLayoutVet(cs(bad).as_ptr(), err2.as_mut_ptr() as *mut c_char, 512);
    let msg = String::from_utf8_lossy(&err2);
    let msg = msg.trim_end_matches('\0').trim();
    println!("vet(bad)   rc={rc}  err=\"{msg}\"  (期望 -1，不打崩进程)");
    assert_eq!(rc, -1, "非法源码 vet 应失败");
    assert!(!msg.is_empty(), "失败应带错误信息");

    // 3) layout 内存源码进 kvspace → 0，返回入口名
    let mut entry = [0u8; 512];
    let mut err3 = [0u8; 512];
    let rc = kvlangLayoutCode(
        cs(good).as_ptr(),
        cs(&dsn).as_ptr(),
        entry.as_mut_ptr() as *mut c_char,
        512,
        err3.as_mut_ptr() as *mut c_char,
        512,
    );
    let entry = String::from_utf8_lossy(&entry);
    let entry = entry.trim_end_matches('\0').trim();
    println!("layoutSrc  rc={rc}  entry=\"{entry}\"  (期望 0)");
    assert_eq!(rc, 0, "合法源码 layout 应成功");

    // 4) 读回 /lib/addup.src —— 自造代码可寻址、可读回
    let mut kv = Kv::conn(&dsn);
    let raw = kv.get_one("/lib/addup.src");
    let src_back = String::from_utf8_lossy(&raw);
    println!("read /lib/addup.src ->\n{}", src_back.trim());
    assert!(src_back.contains("addup"), "读回的源码应含函数名");
    assert!(src_back.contains("add(a, b)"), "读回的源码应是原始 kv 代码");

    // 5) layout 非法源码 → -1，不污染 /lib、不打崩
    let mut err4 = [0u8; 512];
    let rc = kvlangLayoutCode(
        cs(bad).as_ptr(),
        cs(&dsn).as_ptr(),
        std::ptr::null_mut(),
        0,
        err4.as_mut_ptr() as *mut c_char,
        512,
    );
    println!("layoutSrc(bad) rc={rc}  (期望 -1)");
    assert_eq!(rc, -1, "非法源码 layout 应失败");

    println!("\n✅ 全部通过：vet 拦截非法、layout 内存源码入库、.src 可读回。");
}
