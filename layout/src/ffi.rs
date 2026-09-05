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
    fn kvspaceConnect(dsn: *const c_char) -> Handle;
    fn kvspaceClose(h: Handle);
    /// codec 产出为 frontend malloc 缓冲，调用方以 libc free 释放（无 kvspaceBytesFree）。
    fn free(p: *mut c_void);

    /// 借用读：*out 指向后端常驻/回收空间，调用方不得 free。resolve=1 穿透 link。
    fn kvspaceGet(
        h: Handle,
        key: *const c_char,
        resolve: c_int,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    /// 就地写：key 已存在、body_len==原 body_len → 返回原 box body 偏移指针；否则非 0。
    fn kvspaceWriteInPlace(
        h: Handle,
        key: *const c_char,
        resolve: c_int,
        body_len: u32,
        body: *mut *mut u8,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 新位置写：按 (kindexpr, body_len) 分配新 box、写 head，返回 body 偏移指针。
    fn kvspaceWriteNewPlace(
        h: Handle,
        key: *const c_char,
        kindexpr: *const c_char,
        body_len: u32,
        body: *mut *mut u8,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 借用枚举：*out 指向后端常驻/回收缓冲（\n 连接名），调用方不得 free。
    fn kvspaceList(
        h: Handle,
        prefix: *const c_char,
        expand_ext: c_int,
        resolve: c_int,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspaceDel(
        h: Handle,
        keys: *const *const c_char,
        nkeys: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceDelTree(h: Handle, prefix: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceMkindex(h: Handle, path: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceMkindexExt(
        h: Handle,
        path: *const c_char,
        ext_path: *const c_char,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceRmindexExt(h: Handle, path: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceClear(h: Handle, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceDisconnect(h: Handle, err: *mut c_char, err_cap: u32) -> c_int;

    fn kvspaceTlvEncode(
        kind: *const c_char,
        raw: *const u8,
        raw_len: u32,
        dims: *const i32,
        ndim: i32,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspaceDecodeHead(data: *const u8, data_len: u32, out: *mut kvspaceHead_t) -> c_int;

    fn kvspaceNewPtr(
        target_kindexpr: *const c_char,
        target: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspaceNewChar(bytes: *const u8, len: u32, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceNewBool(v: u8, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceNewInt64(v: i64, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceNewFloat64(v: f64, out: *mut *mut u8, out_len: *mut u32) -> c_int;
}

/// XValueHead 解码结果（与 kvspace-durable 的 kvspaceHead_t 布局一致）。kindexpr 为唯一类型真相。
#[repr(C)]
pub struct kvspaceHead_t {
    pub kindexpr: [u8; 256],
    pub ro: u8,
    pub vid: u32,
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

/// codec 调用：产出 frontend malloc 缓冲，拷出后以 libc free 释放。
fn call_codec(f: impl FnOnce(*mut *mut u8, *mut u32) -> c_int) -> Vec<u8> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u32 = 0;
    f(&mut out, &mut out_len);
    if out.is_null() || out_len == 0 {
        return Vec::new();
    }
    let bytes = unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec();
    unsafe { free(out as *mut c_void) };
    bytes
}

/// 借用调用：*out 指向后端常驻/回收空间，拷出自持（借用只需活到本次拷贝），不 free。
fn call_borrow(f: impl FnOnce(*mut *mut u8, *mut u32) -> c_int) -> Vec<u8> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u32 = 0;
    f(&mut out, &mut out_len);
    if out.is_null() || out_len == 0 {
        return Vec::new();
    }
    unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec()
}

// ── 安全句柄 ─────────────────────────────────────────────────────────

/// KVSpace 安全封装（Drop 时释放底层句柄）。
pub struct Kv {
    h: Handle,
}

impl Kv {
    pub fn conn(dsn: &str) -> Kv {
        let c = CString::new(dsn).expect("no NUL in dsn");
        let h = unsafe { kvspaceConnect(c.as_ptr()) };
        Kv { h }
    }

    /// 写：pairs 的值为预编码 TLV；逐条解 head 取 (kindexpr, body)，经 WriteNewPlace
    /// 向 kvspace 要 body 偏移指针后直接写入 body 字节（新建/换 kind/换尺寸唯一原语）。
    pub fn set(&mut self, pairs: &[(String, Vec<u8>)]) -> Result<(), String> {
        for (key, tlv) in pairs {
            self.write_new_place(key, tlv)?;
        }
        Ok(())
    }

    fn write_new_place(&mut self, key: &str, tlv: &[u8]) -> Result<(), String> {
        let h = decode_head(tlv);
        let klen = h.kindexpr.iter().position(|&b| b == 0).unwrap_or(0);
        let kindexpr = CString::new(&h.kindexpr[..klen]).expect("no NUL in kindexpr");
        let ck = CString::new(key).expect("no NUL in key");
        let body_off = h.body_offset as usize;
        let body_len = h.body_len.max(0) as usize;
        let mut body: *mut u8 = std::ptr::null_mut();
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            kvspaceWriteNewPlace(
                self.h,
                ck.as_ptr(),
                kindexpr.as_ptr(),
                body_len as u32,
                &mut body,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        err_ret(&mut err, ret)?;
        if body_len > 0 {
            if body.is_null() {
                return Err(format!("kvspace: WriteNewPlace null body at {key}"));
            }
            unsafe {
                std::ptr::copy_nonoverlapping(tlv[body_off..].as_ptr(), body, body_len);
            }
        }
        Ok(())
    }

    /// 单点读（借用后拷出自持）：None 返回空字节。resolve=1 穿透 link。
    pub fn get_one(&mut self, key: &str) -> Vec<u8> {
        let c = CString::new(key).expect("no NUL in key");
        call_borrow(|out, out_len| unsafe { kvspaceGet(self.h, c.as_ptr(), 0, out, out_len) })
    }

    pub fn list(&mut self, prefix: &str, expand_ext: bool, resolve: bool) -> Vec<String> {
        let c = CString::new(prefix).expect("no NUL in prefix");
        let bytes = call_borrow(|out, out_len| unsafe {
            kvspaceList(
                self.h,
                c.as_ptr(),
                expand_ext as c_int,
                resolve as c_int,
                out,
                out_len,
            )
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
        let ret = unsafe { kvspaceDelTree(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }

    pub fn mkindex(&mut self, path: &str) -> Result<(), String> {
        let c = CString::new(path).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe { kvspaceMkindex(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }

    pub fn ext_index(&mut self, path: &str, ext_path: &str) -> Result<(), String> {
        let cp = CString::new(path).expect("no NUL");
        let ce = CString::new(ext_path).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            kvspaceMkindexExt(
                self.h,
                cp.as_ptr(),
                ce.as_ptr(),
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        err_ret(&mut err, ret)
    }

    pub fn del_ext_index(&mut self, path: &str) -> Result<(), String> {
        let c = CString::new(path).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret =
            unsafe { kvspaceRmindexExt(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }
}

impl Drop for Kv {
    fn drop(&mut self) {
        unsafe { kvspaceClose(self.h) };
    }
}

// ── XValue TLV 编解码（供 kvkind 使用） ─────────────────────────────

/// array_len → dims：char/* 恒一维（含空串/单字符）；其余标量(≤1)=0 维、多元素=1 维。
fn al_to_dims(kind: &str, array_len: i32) -> Vec<i32> {
    if kind.starts_with("char/") {
        vec![array_len.max(0)]
    } else if array_len > 1 {
        vec![array_len]
    } else {
        Vec::new()
    }
}

/// 通用 TLV 编码（内联，ref=0）。
pub fn tlv_encode(kind: &str, raw: &[u8], array_len: i32) -> Vec<u8> {
    let ck = CString::new(kind).expect("no NUL in kind");
    let dims = al_to_dims(kind, array_len);
    call_codec(|out, out_len| unsafe {
        kvspaceTlvEncode(
            ck.as_ptr(),
            raw.as_ptr(),
            raw.len() as u32,
            dims.as_ptr(),
            dims.len() as i32,
            out,
            out_len,
        )
    })
}

/// 解码 XValueHead。
pub fn decode_head(data: &[u8]) -> kvspaceHead_t {
    let mut h = kvspaceHead_t {
        kindexpr: [0u8; 256],
        ro: 0,
        vid: 0,
        body_len: 0,
        body_offset: 0,
    };
    unsafe {
        kvspaceDecodeHead(data.as_ptr(), data.len() as u32, &mut h);
    }
    h
}

// ── 标准标量构造器 ───────────────────────────────────────────────────

pub fn new_ptr(target_kindexpr: &str, target: &str) -> Vec<u8> {
    let ck = CString::new(target_kindexpr).expect("no NUL");
    let ct = CString::new(target).expect("no NUL");
    call_codec(|out, out_len| unsafe { kvspaceNewPtr(ck.as_ptr(), ct.as_ptr(), out, out_len) })
}

pub fn new_char(kind: &str, s: &str) -> Vec<u8> {
    let bytes = s.as_bytes();
    if kind == "char/utf8" {
        return new_char_byte(bytes);
    }
    let (raw, n) = if kind == "char/utf32" {
        let v: Vec<u8> = s.chars().flat_map(|c| (c as u32).to_le_bytes()).collect();
        let n = (v.len() / 4) as i32;
        (v, n)
    } else {
        (bytes.to_vec(), bytes.len() as i32)
    };
    let ck = CString::new(kind).expect("no NUL");
    let dims = [n];
    call_codec(|out, out_len| unsafe {
        kvspaceTlvEncode(
            ck.as_ptr(),
            raw.as_ptr(),
            raw.len() as u32,
            dims.as_ptr(),
            1,
            out,
            out_len,
        )
    })
}

pub fn new_char_byte(bytes: &[u8]) -> Vec<u8> {
    call_codec(|out, out_len| unsafe {
        kvspaceNewChar(bytes.as_ptr(), bytes.len() as u32, out, out_len)
    })
}

pub fn new_bool(v: bool) -> Vec<u8> {
    call_codec(|out, out_len| unsafe { kvspaceNewBool(v as u8, out, out_len) })
}

pub fn new_int64(v: i64) -> Vec<u8> {
    call_codec(|out, out_len| unsafe { kvspaceNewInt64(v, out, out_len) })
}

pub fn new_float64(v: f64) -> Vec<u8> {
    call_codec(|out, out_len| unsafe { kvspaceNewFloat64(v, out, out_len) })
}
