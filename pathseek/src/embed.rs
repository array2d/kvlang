
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
    Lib::new("pathspace", include_bytes!(env!("EMBED_PATHSPACE")), true),
    Lib::new(
        "pathlang_runtime",
        include_bytes!(env!("EMBED_PATHLANG_RUNTIME")),
        false,
    ),
    Lib::new(
        "pathlanglayout",
        include_bytes!(env!("EMBED_PATHLANGLAYOUT")),
        false,
    ),
    
];

fn die(msg: &str) -> ! {
    eprintln!("pathlang-single: {msg}");
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

pub fn init() {
    let mut handles: Vec<(&'static str, *mut c_void)> = Vec::with_capacity(LIBS.len());
    for lib in LIBS.iter() {
        handles.push((lib.name, memfd_load(lib)));
    }
    
    super::ffi::bind(&handles);
    
}
