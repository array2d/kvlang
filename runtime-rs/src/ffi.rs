//! C ABI bindings for kvspace, kvlang runtime, and layout.

use std::ffi::{c_char, c_int, c_void, CStr, CString};

/// Matches kvspaceHead_t.
#[repr(C)]
pub struct KvspaceHead {
    pub headlen: u16,
    pub r#ref: u8,
    pub storetype: u8,
    pub ro: u8,
    pub vid: u32,
    pub body_len: i32,
    pub ndim: i32,
    pub dims: [i32; 8],
    pub langtype: [u8; 256],
    pub langtype_len: i32,
    pub body_offset: i32,
    pub body_cap: u64,
}

impl Default for KvspaceHead {
    fn default() -> Self {
        KvspaceHead {
            headlen: 0,
            r#ref: 0,
            storetype: 0,
            ro: 0,
            vid: 0,
            body_len: 0,
            ndim: 0,
            dims: [0i32; 8],
            langtype: [0u8; 256],
            langtype_len: 0,
            body_offset: 0,
            body_cap: 0,
        }
    }
}

/// Encode through the kvspace codec.
pub fn tlv_encode(kind: &str, raw: &[u8], dims: &[i32]) -> Vec<u8> {
    unsafe {
        let (mut out, mut olen) = (std::ptr::null_mut(), 0u32);
        kvspaceTlvEncode(
            cs(kind).as_ptr(),
            raw.as_ptr(),
            raw.len() as u32,
            if dims.is_empty() {
                std::ptr::null()
            } else {
                dims.as_ptr()
            },
            dims.len() as i32,
            &mut out,
            &mut olen,
        );
        if out.is_null() || olen == 0 {
            return Vec::new();
        }
        let v = std::slice::from_raw_parts(out, olen as usize).to_vec();
        libc::free(out as *mut c_void);
        v
    }
}

unsafe extern "C" {
    // kvspace ABI.
    pub fn kvspaceConnect(dsn: *const c_char) -> *mut c_void;
    pub fn kvspaceClear(h: *mut c_void, err: *mut c_char, err_cap: u32) -> c_int;
    pub fn kvspaceDelTree(
        h: *mut c_void,
        prefix: *const c_char,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn kvspaceDel(
        h: *mut c_void,
        keys: *const *const c_char,
        nkeys: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 借用读：*out 指向后端常驻/回收空间，调用方不得 free。resolve=1 穿透 link。
    pub fn kvspaceGet(
        h: *mut c_void,
        key: *const c_char,
        resolve: c_int,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    pub fn kvspaceSetValue(
        h: *mut c_void,
        key: *const c_char,
        value: *const u8,
        value_len: u32,
        ro: u8,
        vid: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn kvspaceDecodeHead(data: *const u8, data_len: u32, out: *mut KvspaceHead) -> c_int;
    // 前缀遍历：listlen 定计数，逐 idx 取名（借用回收缓冲，不得 free），不一次性返回整段名单。
    pub fn kvspaceListLen(
        h: *mut c_void,
        prefix: *const c_char,
        expand_ext: c_int,
        resolve: c_int,
        out_count: *mut i32,
    ) -> c_int;
    pub fn kvspaceListAt(
        h: *mut c_void,
        prefix: *const c_char,
        expand_ext: c_int,
        resolve: c_int,
        idx: i32,
        buf: *mut u8,
        buf_cap: u32,
        out_len: *mut u32,
    ) -> c_int;
    pub fn kvspaceTlvEncode(
        kind: *const c_char,
        raw: *const u8,
        raw_len: u32,
        dims: *const i32,
        ndim: i32,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    pub fn kvspaceTlvEncodeMode(
        kind: *const c_char,
        raw: *const u8,
        raw_len: u32,
        dims: *const i32,
        ndim: i32,
        r#ref: i32,
        ro: u8,
        vid: u32,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;

    // ── kvlang runtime：模式2 执行（kvlang_vthread.h）───────────────
    pub fn kvlangRuntimeConnect(dsn: *const c_char) -> *mut c_void;
    /// runtime 内部 kvspace 句柄——复用它而非另开连接（durable 惰性 flush 仅同句柄内相干）。
    pub fn kvlangRuntimeKvspaceHandle(rt: *mut c_void) -> *mut c_void;
    pub fn kvlangRuntimeBootstrap(
        rt: *mut c_void,
        funcname: *const c_char,
        args: *const *const c_char,
        nargs: c_int,
    ) -> *mut c_char;
    pub fn kvlangRuntimeExecuteVthread(
        rt: *mut c_void,
        vid: *const c_char,
        out_pc: *mut *mut c_char,
    ) -> c_int;

    // ── kvlang runtime：rwirext 宿主 ABI（均传 kvspace 句柄；kvlang_runtime.h）──
    // C 头 kvlang_runtime.h 导出 9 符号；此处声明 7：故意省略 LangtypeValid/LangtypeMatch
    // ——langtype 校验属 layout 期、匹配属 C dispatch 内部，Rust term 侧不调用（非缺陷）。
    pub fn kvlangDefRwir(
        kvspace: *mut c_void,
        opcode: *const c_char,
        rp: *const *const c_char,
        nr: c_int,
        wp: *const *const c_char,
        nw: c_int,
    ) -> c_int;
    pub fn kvlangRwirextParams(kvspace: *mut c_void, pc: *const c_char) -> *mut c_char;
    pub fn kvlangRwirextResolveRead(
        kvspace: *mut c_void,
        pc: *const c_char,
        idx: c_int,
    ) -> *mut c_char;
    pub fn kvlangRwirextResolveReadPath(
        kvspace: *mut c_void,
        pc: *const c_char,
        idx: c_int,
    ) -> *mut c_char;
    pub fn kvlangRwirextResolveWrite(
        kvspace: *mut c_void,
        pc: *const c_char,
        idx: c_int,
    ) -> *mut c_char;
    pub fn kvlangRwirextNextPc(pc: *const c_char) -> *mut c_char;
    // Handoff：非己方处理的外部 rwir（如 numpy）移交给对应扩展进程。
    pub fn kvlangRwirextHandoff(
        kvspace: *mut c_void,
        vtid: *const c_char,
        pc: *const c_char,
    ) -> c_int;

    // ── kvlang layout：.kv 编译入库 ──────────────────────────────────
    pub fn kvlangLayoutFile(
        path: *const c_char,
        dsn: *const c_char,
        entry: *mut c_char,
        entry_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn kvlangLayoutCode(
        src: *const c_char,
        dsn: *const c_char,
        entry: *mut c_char,
        entry_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn kvlangLayoutFormat(
        src: *const c_char,
        out: *mut c_char,
        out_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn kvlangLayoutVet(src: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    pub fn kvlangLayoutPrintlib(
        lib: *const c_char,
        dsn: *const c_char,
        out: *mut c_char,
        out_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn kvlangLayoutPrintstack(
        vid: *const c_char,
        dsn: *const c_char,
        out: *mut c_char,
        out_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
}

pub fn cs(s: &str) -> CString {
    CString::new(s).unwrap_or_else(|_| CString::new("").unwrap())
}

/// 接管 C runtime / rwirext 返回的字符串（libc malloc，读出后 libc::free）。
/// kvspace 读为借用偏移指针（常驻空间，不 free）；codec 产出为 frontend malloc（libc::free）。
pub fn take(p: *mut c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    let s = unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned();
    unsafe { libc::free(p as *mut c_void) };
    s
}

/// 从定长缓冲区读 NUL 终止字符串（layout 的 entry/err 输出）。
pub fn cbuf(buf: &[u8]) -> String {
    let n = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    String::from_utf8_lossy(&buf[..n]).into_owned()
}
