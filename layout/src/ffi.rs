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
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 读 head + x‥langtype attr key：缺失/空 → 非 0。langtype 为明文事实源。
    fn kvspaceGetHead(h: Handle, key: *const c_char, out: *mut kvspaceHead_t) -> c_int;
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
    /// 固定 8 字节 head 构造器（直接 memcpy struct + body + attr 区；langtype 空 = 空 attr）。
    fn kvspaceXvalueHeadNewNone(out: *mut *mut u8, out_len: *mut u64) -> c_int;
    fn kvspaceXvalueHeadNewScalar(
        storetype: u8,
        raw: *const u8,
        raw_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    fn kvspaceXvalueHeadNewSlack(
        data: *const u8,
        data_len: u64,
        cap: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    fn kvspaceXvalueHeadNewTensor(
        dims: *const u64,
        ndim: u32,
        raw: *const u8,
        raw_len: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    fn kvspaceXvalueHeadNewCode(
        body: *const u8,
        body_len: u64,
        cap: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;

    fn kvspaceNewChar(
        bytes: *const u8,
        len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn kvspaceNewBool(v: u8, langtype: *const c_char, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceNewInt64(v: i64, langtype: *const c_char, out: *mut *mut u8, out_len: *mut u32) -> c_int;
    fn kvspaceNewFloat64(v: f64, langtype: *const c_char, out: *mut *mut u8, out_len: *mut u32) -> c_int;
}

/// XValueHead 解码结果（与 kvspace/include/kvspace/kvspace.h 的 kvspaceHead_t 逐字段对齐）。
/// 三正交轴 ref×storetype×langtype；langtype 为语义类型真相（含 [dims]、无 ptr/ext 前缀），
/// 由 kvspaceGetHead 从 x‥langtype attr key 读入（不在 head 内）。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct kvspaceHead_t {
    pub headlen: u16,
    pub r#ref: u8,
    pub storetype: u8,
    pub body_len: i32,
    pub ndim: i32,
    pub dims: [i32; 8],
    pub langtype: [u8; 256],
    pub langtype_len: i32,
    pub body_offset: i32,
    pub body_cap: u64,
}

// ── 原位 head 访问（v2 固定 8 字节头，#566：偏移直取，不建结构、不校验）──

/// wire: [len:u48 LE@0-5][storetype:u8@6][ptr:u8@7][body@8]；body 偏移恒 = 8。
pub const PREFIX: usize = 8;

/// u48 小端 body 长度（offset 0-5）。data 过短 → 0。
#[inline]
pub fn xh_len(data: &[u8]) -> u64 {
    let b = data.get(0..6).unwrap_or(&[]);
    let mut v = 0u64;
    for i in (0..b.len()).rev() {
        v = (v << 8) | u64::from(b[i]);
    }
    v
}
/// storetype（offset 6）；data 过短 → 0。
#[inline]
pub fn xh_storetype(data: &[u8]) -> u8 {
    data.get(6).copied().unwrap_or(0)
}
/// ptr（offset 7）：0 inline / 1 PTR / 2 EXT；data 过短 → 0。
#[inline]
pub fn xh_ptr(data: &[u8]) -> u8 {
    data.get(7).copied().unwrap_or(0)
}
#[inline]
pub fn xh_is_ptr(data: &[u8]) -> bool {
    xh_ptr(data) == 1
}
/// None：空字节，或固定头 len=0 且非 ptr/ext（KVSPACE_XVALUE_PTR_INLINE 的 SHORT 空值）。
#[inline]
pub fn xh_is_none(data: &[u8]) -> bool {
    data.is_empty() || (data.len() >= PREFIX && xh_len(data) == 0 && xh_ptr(data) == 0)
}

/// body 切片（偏移恒 = PREFIX，长度 = len）；越界 → 空。
pub fn xh_body_slice(data: &[u8]) -> &[u8] {
    let len = xh_len(data) as usize;
    let Some(end) = PREFIX.checked_add(len) else {
        return &[];
    };
    if end > data.len() {
        return &[];
    }
    &data[PREFIX..end]
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

/// 固定头构造器调用（New* 的 out_len 为 u64）：成功且 total<=u32::MAX 才转 u32。
fn call_codec64(f: impl FnOnce(*mut *mut u8, *mut u64) -> c_int) -> Vec<u8> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u64 = 0;
    f(&mut out, &mut out_len);
    if out.is_null() || out_len == 0 || out_len > u32::MAX as u64 {
        if !out.is_null() {
            unsafe { free(out as *mut c_void) };
        }
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

fn connect_or_panic(dsn: &str) -> Handle {
    let c = CString::new(dsn).expect("no NUL in dsn");
    let h = unsafe { kvspaceConnect(c.as_ptr()) };
    if h.is_null() {
        panic!("kvspace connect failed: {dsn}");
    }
    h
}

/// KVSpace 安全封装（Drop 时释放底层句柄）。单数据 store，无控制 store。
pub struct Kv {
    h: Handle,
}

impl Kv {
    pub fn conn(dsn: &str) -> Kv {
        Kv {
            h: connect_or_panic(dsn),
        }
    }

    /// 写多对 (key, langtype, wire)：langtype 拼入值 attr 区整体单写（无 attr key）。
    /// wire 空字节 = None → 删该 key。8 字节空头是目录 marker 等合法值（len=0 非
    /// None），照常写——None 语义恒空字节（[[kvlang-layout-rust]]）。
    pub fn set(&mut self, pairs: &[(String, String, Vec<u8>)]) -> Result<(), String> {
        for (key, lt, xvalue) in pairs {
            // 末段含 ‥ 的 key 是系统变量本体：无类型（保留 codec 空 attr）。
            // 前缀可含 ‥（如 /‥isa/），只拦末段。
            let last_seg = key.rsplit('/').next().unwrap_or(key);
            let wire = if xvalue.is_empty() || lt.is_empty() || last_seg.contains('\u{2025}') {
                xvalue.clone()
            } else {
                // 用真实 langtype 替换 codec 产出的空 attr 占位：[head+body] + attr(lt)。
                let body_len = xh_len(xvalue) as usize;
                let mut w = xvalue[..PREFIX + body_len].to_vec();
                w.extend_from_slice(&attr_bytes(lt));
                w
            };
            self.set_one(key, &wire)?;
        }
        Ok(())
    }

    fn set_one(&mut self, key: &str, xvalue: &[u8]) -> Result<(), String> {
        let ck = CString::new(key).expect("no NUL in key");
        let mut err: [c_char; 256] = [0; 256];
        let is_none = xvalue.is_empty(); // None 恒空字节（8 字节空头 = 目录 marker 等真值）
        let ret = if is_none {
            let key_ptr = ck.as_ptr();
            unsafe { kvspaceDel(self.h, &key_ptr, 1, err.as_mut_ptr(), err.len() as u32) }
        } else {
            unsafe {
                kvspaceSetValue(
                    self.h,
                    ck.as_ptr(),
                    xvalue.as_ptr(),
                    xvalue.len() as u32,
                    err.as_mut_ptr(),
                    err.len() as u32,
                )
            }
        };
        err_ret(&mut err, ret)
    }

    /// 单点读（借用后拷出自持）：None 返回空字节。resolve=1 穿透 link。
    pub fn get_one(&mut self, key: &str) -> Vec<u8> {
        let c = CString::new(key).expect("no NUL in key");
        call_borrow(|out, out_len| unsafe { kvspaceGet(self.h, c.as_ptr(), 0, out, out_len) })
    }

    /// 读 head + x‥langtype attr key（缺失/空 → 返回默认空 head）。
    pub fn get_head(&mut self, key: &str) -> kvspaceHead_t {
        let mut h = kvspaceHead_t {
            headlen: 0,
            r#ref: 0,
            storetype: 0,
            body_len: 0,
            ndim: 0,
            dims: [0i32; 8],
            langtype: [0u8; 256],
            langtype_len: 0,
            body_offset: 0,
            body_cap: 0,
        };
        let c = CString::new(key).expect("no NUL in key");
        let ret = unsafe { kvspaceGetHead(self.h, c.as_ptr(), &mut h) };
        if ret != 0 {
            h.langtype_len = 0;
        }
        h
    }

    /// 读 key 的完整 langtype（attr key 明文事实源）；无/空 → 空串。
    pub fn langtype_of(&mut self, key: &str) -> String {
        let h = self.get_head(key);
        let end = h
            .langtype
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(h.langtype.len());
        String::from_utf8_lossy(&h.langtype[..end]).into_owned()
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

/// 空 C 串（codec langtype 参数：空 = 空 attr 占位，真实类型由 Kv::set 按 pair 的 lt 写入）。
pub static EMPTY_CSTR: [u8; 1] = [0];

/// attr 区字节（#571）：langtype 记录（含 attr_len 前缀）；空 lt = 空 attr `[2,0]`。
pub fn attr_bytes(lt: &str) -> Vec<u8> {
    if lt.is_empty() {
        return vec![2, 0];
    }
    let alen = 2u16 + 3 + lt.len() as u16;
    let mut v = Vec::with_capacity(5 + lt.len());
    v.extend_from_slice(&alen.to_le_bytes());
    v.push(1); // tag = langtype
    v.extend_from_slice(&(lt.len() as u16).to_le_bytes());
    v.extend_from_slice(lt.as_bytes());
    v
}

/// 按 kind+body 产 wire（固定 8 字节 head + body + 空 attr 占位；真实类型由 Kv::set 按
/// pair 的 lt 替换）。storetype 判定对齐 durable to_wire_head：char/* 与 rwir 族→SLACK、
/// ndim>0 非容器→LARGE、其余标量→SHORT。
pub fn xvalue_to_wire(kind: &str, raw: &[u8], array_len: i32) -> Vec<u8> {
    let dims = al_to_dims(kind, array_len);
    if (kind.is_empty() || kind == "None") && raw.is_empty() {
        return call_codec64(|out, out_len| unsafe {
            kvspaceXvalueHeadNewNone(out, out_len)
        });
    }
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    if kind == "char" || kind.starts_with("char/") {
        return call_codec64(|out, out_len| unsafe {
            kvspaceXvalueHeadNewSlack(
                raw.as_ptr(),
                raw.len() as u64,
                raw.len() as u64,
                empty,
                out,
                out_len,
            )
        });
    }
    if !dims.is_empty() && !kind.contains('·') && !kind.starts_with('/') {
        let d: Vec<u64> = dims.iter().map(|&x| x as u64).collect();
        return call_codec64(|out, out_len| unsafe {
            kvspaceXvalueHeadNewTensor(d.as_ptr(), d.len() as u32, raw.as_ptr(), raw.len() as u64, empty, out, out_len)
        });
    }
    if kind == "rwir" || kind == "def langtype" || (kind == "rwfunc" && !raw.is_empty()) {
        return call_codec64(|out, out_len| unsafe {
            kvspaceXvalueHeadNewCode(
                raw.as_ptr(),
                raw.len() as u64,
                raw.len() as u64,
                empty,
                out,
                out_len,
            )
        });
    }
    call_codec64(|out, out_len| unsafe {
        kvspaceXvalueHeadNewScalar(0 /* KVSPACE_XVALUE_ST_SHORT */, raw.as_ptr(), raw.len() as u32, empty, out, out_len)
    })
}

/// 按 wire 字节原位填 head 视图（v2 固定 8 字节头：#566）。
/// langtype 不在 head——由调用方从 kvspaceGetHead（x‥langtype attr key）读入后传入。
pub fn from_wire_head(data: &[u8], langtype: &str) -> kvspaceHead_t {
    let mut h = kvspaceHead_t {
        headlen: 0,
        r#ref: 0,
        storetype: 0,
        body_len: 0,
        ndim: 0,
        dims: [0i32; 8],
        langtype: [0u8; 256],
        langtype_len: 0,
        body_offset: 0,
        body_cap: 0,
    };
    if data.len() < PREFIX {
        return h;
    }
    let len = xh_len(data);
    let storetype = data[6];
    let ptr = data[7];
    if storetype > 3 || ptr > 2 {
        return h;
    }
    h.headlen = PREFIX as u16;
    h.r#ref = if ptr == 1 {
        1
    } else if ptr == 2 {
        2
    } else {
        0
    };
    h.storetype = storetype;
    h.body_len = i32::try_from(len).unwrap_or(i32::MAX);
    h.body_offset = PREFIX as i32;
    h.body_cap = len;
    let n = langtype.len().min(255);
    h.langtype[..n].copy_from_slice(&langtype.as_bytes()[..n]);
    h.langtype_len = n as i32;
    h
}

// ── 标准标量构造器 ───────────────────────────────────────────────────

pub fn new_char(kind: &str, s: &str) -> Vec<u8> {
    let bytes = s.as_bytes();
    if kind == "char/utf8" {
        return new_char_byte(bytes);
    }
    let raw = if kind == "char/utf32" {
        s.chars().flat_map(|c| (c as u32).to_le_bytes()).collect()
    } else {
        bytes.to_vec()
    };
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec64(|out, out_len| unsafe {
        kvspaceXvalueHeadNewSlack(
            raw.as_ptr(),
            raw.len() as u64,
            raw.len() as u64,
            empty,
            out,
            out_len,
        )
    })
}

pub fn new_char_byte(bytes: &[u8]) -> Vec<u8> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec(|out, out_len| unsafe {
        kvspaceNewChar(bytes.as_ptr(), bytes.len() as u32, empty, out, out_len)
    })
}

pub fn new_bool(v: bool) -> Vec<u8> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec(|out, out_len| unsafe { kvspaceNewBool(v as u8, empty, out, out_len) })
}

pub fn new_int64(v: i64) -> Vec<u8> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec(|out, out_len| unsafe { kvspaceNewInt64(v, empty, out, out_len) })
}

pub fn new_float64(v: f64) -> Vec<u8> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec(|out, out_len| unsafe { kvspaceNewFloat64(v, empty, out, out_len) })
}
