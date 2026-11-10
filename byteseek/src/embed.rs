//! #493 单一二进制：自有 `.so` 以字节嵌进可执行文件，启动时用 memfd 从内存加载。
//!
//! 顺序：前端（RTLD_GLOBAL）→ runtime-c → layout → kvspace-c → durable。
//! runtime-c 与 layout 的 NEEDED 是 `libkvspace.so.1`：前端先加载并 RTLD_GLOBAL，
//! loader 按 DT_SONAME 命中内存实例；后端只 NEEDED libc，保持 RTLD_LOCAL，避免它们
//! 的同名 kvspace* 覆盖前端的全局符号。
//! memfd 无路径、不落盘。dispatch 前端在 connect 时用
//! `dlopen(soname, RTLD_NOW | RTLD_NOLOAD)` 复用这里已加载的后端；
//! 运行时不需要任何额外环境变量——只有 KVSPACE。

use std::ffi::{CStr, CString};
use std::os::raw::c_void;
use std::sync::atomic::{AtomicI32, Ordering};

struct Lib {
    name: &'static str,
    bytes: &'static [u8],
    global: bool,
    fd: AtomicI32,
}

impl Lib {
    const fn new(name: &'static str, bytes: &'static [u8], global: bool) -> Self {
        Lib {
            name,
            bytes,
            global,
            fd: AtomicI32::new(-1),
        }
    }
}

static LIBS: [Lib; 3] = [
    Lib::new("kvspace", include_bytes!(env!("EMBED_KVSPACE")), true),
    Lib::new(
        "kvlang_runtime",
        include_bytes!(env!("EMBED_KVLANG_RUNTIME")),
        false,
    ),
    Lib::new(
        "kvlanglayout",
        include_bytes!(env!("EMBED_KVLANGLAYOUT")),
        false,
    ),
    // `kvspace-c` 与 `kvspace_durable` 不再嵌入：单库化后 shm 引擎直接编在
    // libkvspace 里，`kvspaceConnect` 不 dlopen 任何后端。嵌进去的两个字节块
    // 没有任何代码会去 dlopen 它们——是纯粹的体积。
];

fn die(msg: &str) -> ! {
    eprintln!("kvlang-single: {msg}");
    std::process::abort()
}

fn memfd_load(lib: &Lib) -> *mut c_void {
    let cname = CString::new(lib.name).unwrap();
    let fd = unsafe { libc::memfd_create(cname.as_ptr(), 0) };
    if fd < 0 {
        die(&format!(
            "memfd_create({}): {}",
            lib.name,
            std::io::Error::last_os_error()
        ));
    }
    let bytes = lib.bytes;
    let mut off = 0usize;
    while off < bytes.len() {
        let n = unsafe {
            libc::write(
                fd,
                bytes[off..].as_ptr() as *const c_void,
                bytes.len() - off,
            )
        };
        if n <= 0 {
            die(&format!(
                "write({}): {}",
                lib.name,
                std::io::Error::last_os_error()
            ));
        }
        off += n as usize;
    }
    let path = CString::new(format!("/proc/self/fd/{fd}")).unwrap();
    let flags = libc::RTLD_NOW | if lib.global { libc::RTLD_GLOBAL } else { 0 };
    let h = unsafe { libc::dlopen(path.as_ptr(), flags) };
    if h.is_null() {
        let e = unsafe { libc::dlerror() };
        let msg = if e.is_null() {
            "unknown dlopen error".to_string()
        } else {
            unsafe { CStr::from_ptr(e) }.to_string_lossy().into_owned()
        };
        die(&format!("dlopen({}): {msg}", lib.name));
    }
    lib.fd.store(fd, Ordering::Relaxed);
    h
}

/// 启动第一件事：加载全部嵌入库、绑定 shim、把后端的完整路径指到 memfd。
pub fn init() {
    let mut handles: Vec<(&'static str, *mut c_void)> = Vec::with_capacity(LIBS.len());
    for lib in LIBS.iter() {
        handles.push((lib.name, memfd_load(lib)));
    }
    // 29 个导入符号全部按名绑定；miss 即硬错（bind 内部 abort）。
    super::ffi::bind(&handles);
    // 两个后端已按 SONAME 加载：前端 connect 时 RTLD_NOLOAD 直接复用。
}
