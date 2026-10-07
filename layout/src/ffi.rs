//! KVSpace C ABI bindings for layout.

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};

// ── 不透明句柄 ─────────────────────────────────────────────────────────

/// kvspace-durable 侧 `*mut Box<dyn KVSpace>` 的不透明视图。
pub type Handle = *mut c_void;

// ── extern "C" 声明 ─────────────────────────────────────────────────────

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
    fn kvspaceSetValue(
        h: Handle,
        key: *const c_char,
        value: *const u8,
        value_len: u32,
        ro: u8,
        vid: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceDel(
        h: Handle,
        keys: *const *const c_char,
        nkeys: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 前缀遍历：listlen 定计数，逐 idx 取名（借用回收缓冲，不得 free），不一次性返回整段名单。
    fn kvspaceListLen(
        h: Handle,
        prefix: *const c_char,
        expand_ext: c_int,
        resolve: c_int,
        out_count: *mut i32,
    ) -> c_int;
    fn kvspaceListAt(
        h: Handle,
        prefix: *const c_char,
        expand_ext: c_int,
        resolve: c_int,
        idx: i32,
        buf: *mut u8,
        buf_cap: u32,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspaceDelTree(h: Handle, prefix: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceMkindex(
        h: Handle,
        path: *const c_char,
        capacity: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceTlvEncode(
        kind: *const c_char,
        raw: *const u8,
        raw_len: u32,
        dims: *const i32,
        ndim: i32,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    #[allow(dead_code)] // 保留绑定（外部 ABI 用）；调用点已改为原位字节访问。
    fn kvspaceDecodeHead(data: *const u8, data_len: u32, out: *mut kvspaceHead_t) -> c_int;

    fn kvspaceNewChar(bytes: *const u8, len: u32, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceNewBool(v: u8, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceNewInt64(v: i64, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceNewFloat64(v: f64, out: *mut *mut u8, out_len: *mut u32) -> c_int;
}

/// XValueHead 解码结果（与 kvspace/include/kvspace/kvspace.h 的 kvspaceHead_t 逐字段对齐）。
/// 三正交轴 ref×storetype×langtype；langtype 为语义类型真相（含 [dims]、无 ptr/ext 前缀）。
#[repr(C)]
pub struct kvspaceHead_t {
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

// ── 原位 head 访问（与 C 侧 xh_* 对齐：偏移直取，不建 328B head、不校验）──

/// wire: [pow:u8][flags:u8][a:u64le][b:u64le][langtype][padding][body]。
pub const XH_PREFIX: usize = 18;

#[inline]
fn xh_headlen(data: &[u8]) -> usize {
    1usize << data[0]
}
#[inline]
fn xh_class(data: &[u8]) -> u8 {
    data[1] & 3
}
#[inline]
pub fn xh_is_ptr(data: &[u8]) -> bool {
    data[1] & 4 != 0
}
#[inline]
pub fn xh_is_none(data: &[u8]) -> bool {
    data.len() > XH_PREFIX && data[1] == 0 && data[XH_PREFIX] == 0
}
#[inline]
fn xh_a(data: &[u8]) -> u64 {
    u64::from_le_bytes(data[2..10].try_into().unwrap())
}
#[inline]
fn xh_b(data: &[u8]) -> u64 {
    u64::from_le_bytes(data[10..18].try_into().unwrap())
}

/// wire 内 langtype 串（headlen 内首个 NUL 前）。data 过短 → ""。
pub fn xh_langtype(data: &[u8]) -> String {
    if data.len() <= XH_PREFIX {
        return String::new();
    }
    let hl = xh_headlen(data).min(data.len());
    let end = data[XH_PREFIX..hl]
        .iter()
        .position(|&b| b == 0)
        .map(|i| XH_PREFIX + i)
        .unwrap_or(hl);
    String::from_utf8_lossy(&data[XH_PREFIX..end]).into_owned()
}

/// class0 标量宽度（按 base kind 查表；def rwir 特例 5；其余 0）。
fn scalar_width(kind: &str) -> usize {
    match kind {
        "bool" | "int8" | "uint8" => 1,
        "int16" | "uint16" => 2,
        "int32" | "uint32" | "float32" => 4,
        "int64" | "uint64" | "float64" | "time" | "duration" => 8,
        "def rwir" => 5,
        _ => 0,
    }
}

/// body 内容长度：class0 标量宽度 / class1 slack 与 class3 ext = a / class2 tensor = a*b。
pub fn xh_content_len(data: &[u8]) -> usize {
    if data.len() < XH_PREFIX {
        return 0;
    }
    match xh_class(data) {
        0 => scalar_width(&xh_langtype(data)),
        2 => (xh_a(data) * xh_b(data)) as usize,
        _ => xh_a(data) as usize,
    }
}

/// body 切片（按 headlen 偏移 + class 推导长度）；越界 → 空。
pub fn xh_body_slice(data: &[u8]) -> &[u8] {
    if data.len() < XH_PREFIX {
        return &[];
    }
    let off = xh_headlen(data);
    let len = xh_content_len(data);
    if off + len > data.len() {
        return &[];
    }
    &data[off..off + len]
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

    pub fn set(&mut self, pairs: &[(String, Vec<u8>)]) -> Result<(), String> {
        for (key, tlv) in pairs {
            let ck = CString::new(key.as_str()).expect("no NUL in key");
            let mut err: [c_char; 256] = [0; 256];
            let is_none = tlv.is_empty() || xh_is_none(tlv);
            let ret = if is_none {
                let key_ptr = ck.as_ptr();
                unsafe { kvspaceDel(self.h, &key_ptr, 1, err.as_mut_ptr(), err.len() as u32) }
            } else {
                unsafe {
                    kvspaceSetValue(
                        self.h,
                        ck.as_ptr(),
                        tlv.as_ptr(),
                        tlv.len() as u32,
                        0,
                        0,
                        err.as_mut_ptr(),
                        err.len() as u32,
                    )
                }
            };
            err_ret(&mut err, ret)?;
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
        let mut count: i32 = 0;
        if unsafe {
            kvspaceListLen(
                self.h,
                c.as_ptr(),
                expand_ext as c_int,
                resolve as c_int,
                &mut count,
            )
        } != 0
            || count <= 0
        {
            return Vec::new();
        }
        let mut v = Vec::with_capacity(count as usize);
        for i in 0..count {
            let mut buf = [0u8; 1024];
            let mut out_len: u32 = 0;
            let ok = unsafe {
                kvspaceListAt(
                    self.h,
                    c.as_ptr(),
                    expand_ext as c_int,
                    resolve as c_int,
                    i,
                    buf.as_mut_ptr(),
                    buf.len() as u32,
                    &mut out_len,
                )
            } == 0;
            if ok && out_len > 0 {
                v.push(String::from_utf8_lossy(&buf[..out_len as usize]).into_owned());
            }
        }
        v
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
        let ret =
            unsafe { kvspaceMkindex(self.h, c.as_ptr(), 0, err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }
}

impl Drop for Kv {
    fn drop(&mut self) {
        unsafe { kvspaceClose(self.h) };
    }
}

// XValue codec.

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

/// Encode an inline XValue.
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

/// 按 wire 字节原位填 head 视图（不调用 kvspaceDecodeHead、不校验）。
pub fn decode_head(data: &[u8]) -> kvspaceHead_t {
    let mut h = kvspaceHead_t {
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
    };
    if data.len() < XH_PREFIX {
        return h;
    }
    let hl = xh_headlen(data);
    h.headlen = hl as u16;
    h.r#ref = if xh_is_ptr(data) {
        1
    } else if xh_class(data) == 3 {
        2
    } else {
        0
    };
    h.storetype = xh_class(data);
    let bl = xh_content_len(data);
    h.body_len = bl as i32;
    h.body_offset = hl as i32;
    h.body_cap = bl as u64;
    let lt = xh_langtype(data);
    let n = lt.len().min(255);
    h.langtype[..n].copy_from_slice(&lt.as_bytes()[..n]);
    h.langtype_len = n as i32;
    h
}

// ── 标准标量构造器 ───────────────────────────────────────────────────

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
