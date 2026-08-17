//! kvspace-durable 的 C ABI 绑定 + 安全封装。
//!
//! 布局侧不依赖 kvspace-durable 的 Rust 类型，只通过 `extern "C"` 符号表调用。
//! 所有 XValue 以 TLV 字节（`Vec<u8>`）跨边界；空字节 = None。

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};

// ── 不透明句柄 ─────────────────────────────────────────────────────────

/// kvspace-durable 侧 `*mut Box<dyn KVSpace>` 的不透明视图。
pub type Handle = *mut c_void;

// ── extern "C" 声明 ─────────────────────────────────────────────────────

#[allow(dead_code)]
extern "C" {
    fn kvspace_conn(dsn: *const c_char) -> Handle;
    fn kvspace_free(h: Handle);
    fn kvspace_bytes_free(p: *mut u8, len: u32);

    fn kvspace_set(
        h: Handle,
        keys: *const *const c_char,
        vals: *const u8,
        lens: *const u32,
        n: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspace_get_one(
        h: Handle,
        key: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspace_list(
        h: Handle,
        prefix: *const c_char,
        expand_ext: c_int,
        resolve: c_int,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspace_del(
        h: Handle,
        keys: *const *const c_char,
        nkeys: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspace_del_tree(h: Handle, prefix: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspace_mkindex(h: Handle, path: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspace_ext_index(
        h: Handle,
        path: *const c_char,
        ext_path: *const c_char,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspace_del_ext_index(h: Handle, path: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspace_clear(h: Handle, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspace_disconn(h: Handle, err: *mut c_char, err_cap: u32) -> c_int;

    fn kvspace_tlv_encode(
        kind: *const c_char,
        raw: *const u8,
        raw_len: u32,
        array_len: i32,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspace_tlv_encode_ptr(
        kind: *const c_char,
        raw: *const u8,
        raw_len: u32,
        array_len: i32,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspace_decode_head(data: *const u8, data_len: u32, out: *mut KVHead) -> c_int;

    fn kvspace_new_ptr(
        kind: *const c_char,
        target: *const c_char,
        array_len: i32,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspace_new_char(
        kind: *const c_char,
        s: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspace_new_char_byte(bytes: *const u8, len: u32, out: *mut *mut u8, out_len: *mut u32)
        -> c_int;
    fn kvspace_new_bool(v: u8, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspace_new_int64(v: i64, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspace_new_float64(v: f64, out: *mut *mut u8, out_len: *mut u32) -> c_int;
}

/// XValueHead 解码结果（与 kvspace-durable 的 KVHead 布局一致）。
#[repr(C)]
pub struct KVHead {
    pub kind: [u8; 32],
    pub is_ptr: u8,
    pub array_len: i32,
    pub body_len: i32,
    pub body_offset: i32,
}

// ── 内部助手 ─────────────────────────────────────────────────────────

fn err_ret(buf: &mut [c_char; 256], ret: c_int) -> Result<(), String> {
    if ret == 0 {
        return Ok(());
    }
    let msg = unsafe { CStr::from_ptr(buf.as_ptr()) }
        .to_string_lossy()
        .into_owned();
    Err(if msg.is_empty() {
        "kvspace: error".to_string()
    } else {
        msg
    })
}

/// 调用带 (out, out_len) 输出参数的 extern fn，返回分配的字节并释放。
fn call_alloc(f: impl FnOnce(*mut *mut u8, *mut u32) -> c_int) -> Vec<u8> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u32 = 0;
    f(&mut out, &mut out_len);
    if out.is_null() || out_len == 0 {
        return Vec::new();
    }
    let bytes = unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec();
    unsafe { kvspace_bytes_free(out, out_len) };
    bytes
}

fn to_cstrings(ss: &[String]) -> (Vec<CString>, Vec<*const c_char>) {
    let cs: Vec<CString> = ss
        .iter()
        .map(|s| CString::new(s.as_str()).expect("no NUL in key"))
        .collect();
    let ptrs: Vec<*const c_char> = cs.iter().map(|c| c.as_ptr()).collect();
    (cs, ptrs)
}

// ── 安全句柄 ─────────────────────────────────────────────────────────

/// KVSpace 安全封装（Drop 时释放底层句柄）。
pub struct Kv {
    h: Handle,
}

impl Kv {
    pub fn conn(dsn: &str) -> Kv {
        let c = CString::new(dsn).expect("no NUL in dsn");
        let h = unsafe { kvspace_conn(c.as_ptr()) };
        Kv { h }
    }

    pub fn set(&mut self, pairs: &[(String, Vec<u8>)]) -> Result<(), String> {
        let keys: Vec<String> = pairs.iter().map(|(k, _)| k.clone()).collect();
        let (_cs, key_ptrs) = to_cstrings(&keys);
        let mut vals: Vec<u8> = Vec::new();
        let mut lens: Vec<u32> = Vec::new();
        for (_, v) in pairs {
            vals.extend_from_slice(v);
            lens.push(v.len() as u32);
        }
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            kvspace_set(
                self.h,
                key_ptrs.as_ptr(),
                vals.as_ptr(),
                lens.as_ptr(),
                lens.len() as u32,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        err_ret(&mut err, ret)
    }

    /// 单点读：None 返回空字节。
    pub fn get_one(&mut self, key: &str) -> Vec<u8> {
        let c = CString::new(key).expect("no NUL in key");
        call_alloc(|out, out_len| unsafe { kvspace_get_one(self.h, c.as_ptr(), out, out_len) })
    }

    pub fn list(&mut self, prefix: &str, expand_ext: bool, resolve: bool) -> Vec<String> {
        let c = CString::new(prefix).expect("no NUL in prefix");
        let bytes = call_alloc(|out, out_len| unsafe {
            kvspace_list(self.h, c.as_ptr(), expand_ext as c_int, resolve as c_int, out, out_len)
        });
        if bytes.is_empty() {
            return Vec::new();
        }
        String::from_utf8_lossy(&bytes)
            .split('\n')
            .map(|s| s.to_string())
            .collect()
    }

    pub fn del_tree(&mut self, prefix: &str) -> Result<(), String> {
        let c = CString::new(prefix).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe { kvspace_del_tree(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }

    pub fn mkindex(&mut self, path: &str) -> Result<(), String> {
        let c = CString::new(path).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe { kvspace_mkindex(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }

    pub fn ext_index(&mut self, path: &str, ext_path: &str) -> Result<(), String> {
        let cp = CString::new(path).expect("no NUL");
        let ce = CString::new(ext_path).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            kvspace_ext_index(self.h, cp.as_ptr(), ce.as_ptr(), err.as_mut_ptr(), err.len() as u32)
        };
        err_ret(&mut err, ret)
    }

    pub fn del_ext_index(&mut self, path: &str) -> Result<(), String> {
        let c = CString::new(path).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            kvspace_del_ext_index(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32)
        };
        err_ret(&mut err, ret)
    }
}

impl Drop for Kv {
    fn drop(&mut self) {
        unsafe { kvspace_free(self.h) };
    }
}

// ── XValue TLV 编解码（供 kvkind 使用） ─────────────────────────────

/// 通用 TLV 编码（内联，ref=0）。
pub fn tlv_encode(kind: &str, raw: &[u8], array_len: i32) -> Vec<u8> {
    let ck = CString::new(kind).expect("no NUL in kind");
    call_alloc(|out, out_len| unsafe {
        kvspace_tlv_encode(ck.as_ptr(), raw.as_ptr(), raw.len() as u32, array_len, out, out_len)
    })
}

/// 通用 TLV 编码（软链接，ref=1）。
pub fn tlv_encode_ptr(kind: &str, raw: &[u8], array_len: i32) -> Vec<u8> {
    let ck = CString::new(kind).expect("no NUL in kind");
    call_alloc(|out, out_len| unsafe {
        kvspace_tlv_encode_ptr(ck.as_ptr(), raw.as_ptr(), raw.len() as u32, array_len, out, out_len)
    })
}

/// 解码 XValueHead。
pub fn decode_head(data: &[u8]) -> KVHead {
    let mut h = KVHead {
        kind: [0u8; 32],
        is_ptr: 0,
        array_len: 0,
        body_len: 0,
        body_offset: 0,
    };
    unsafe {
        kvspace_decode_head(data.as_ptr(), data.len() as u32, &mut h);
    }
    h
}

// ── 标准标量构造器 ───────────────────────────────────────────────────

pub fn new_ptr(kind: &str, target: &str, array_len: i32) -> Vec<u8> {
    let ck = CString::new(kind).expect("no NUL");
    let ct = CString::new(target).expect("no NUL");
    call_alloc(|out, out_len| unsafe {
        kvspace_new_ptr(ck.as_ptr(), ct.as_ptr(), array_len, out, out_len)
    })
}

pub fn new_char(kind: &str, s: &str) -> Vec<u8> {
    let ck = CString::new(kind).expect("no NUL");
    let cs = CString::new(s).expect("no NUL");
    call_alloc(|out, out_len| unsafe {
        kvspace_new_char(ck.as_ptr(), cs.as_ptr(), out, out_len)
    })
}

pub fn new_char_byte(bytes: &[u8]) -> Vec<u8> {
    call_alloc(|out, out_len| unsafe {
        kvspace_new_char_byte(bytes.as_ptr(), bytes.len() as u32, out, out_len)
    })
}

pub fn new_bool(v: bool) -> Vec<u8> {
    call_alloc(|out, out_len| unsafe { kvspace_new_bool(v as u8, out, out_len) })
}

pub fn new_int64(v: i64) -> Vec<u8> {
    call_alloc(|out, out_len| unsafe { kvspace_new_int64(v, out, out_len) })
}

pub fn new_float64(v: f64) -> Vec<u8> {
    call_alloc(|out, out_len| unsafe { kvspace_new_float64(v, out, out_len) })
}
