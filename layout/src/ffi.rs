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
    /// 固定 16 字节 head 构造器（直接 memcpy struct + body + attr 区；langtype 空 = 空 attr）。
    fn kvspaceXvalueHeadNewNone(out: *mut *mut u8, out_len: *mut u64) -> c_int;
    /// **没有 `storetype` 形参**：head 的 storetype 由构造器按 kind 自己定，
    /// 调用方不再决定。签名以 `kvspace/include/kvspace/xvalue_head.h` 为准。
    ///
    /// 早先这里多传一个 `storetype: u8`，而新库已经改成 4 个形参——
    /// **实参个数与形参个数不符是 UB**：Debug 构建侥幸不崩，Release 必段错误
    /// （14:0x 实测，`kvspaceXvalueHeadNewScalar` 里 SIGSEGV）。
    fn kvspaceXvalueHeadNewScalar(
        raw: *const u8,
        raw_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    fn kvspaceXvalueHeadNewSlack(
        data: *const u8,
        data_len: u64,
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

// ── 原位 head 访问（16 字节固定头：长度全在 head 里，attr 区无前缀）──

/// wire: [bodylen:u64 @0-7][attrslen:u16 @8-9][padding:6 @10-15][body][attr records]；
/// body 偏移恒 = 16。总长 = 16 + bodylen + attrslen。padding 必须为零。
///
/// **head 只描述「字节怎么读」**（`kvspace/include/kvspace/xvalue_head.h`）：
/// bodylen 是 body 区字节数，它是容量还是有效长度由 **langtype** 判（bufferarray ⇒
/// 容量，定长数组/标量 ⇒ 有效长度），head 不承载。`storetype` / `ptr` / `layout`
/// 三个字段先后作废，现在头里只有两个字段加 padding。
pub const PREFIX: usize = 16;

const OFF_ATTRSLEN: usize = 8;
/// padding 的起始偏移（6 字节，到 15 为止）。
const OFF_PADDING: usize = 10;

/// body 长度（u64 小端 @0-7）。data 过短 → 0。
#[inline]
pub fn xh_len(data: &[u8]) -> u64 {
    match data.get(0..8) {
        Some(b) => u64::from_le_bytes([b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]]),
        None => 0,
    }
}
/// attr 区字节数（u16 小端 @8-9）；data 过短 → 0。
#[inline]
pub fn xh_attrs_len(data: &[u8]) -> u16 {
    match data.get(OFF_ATTRSLEN..OFF_ATTRSLEN + 2) {
        Some(b) => u16::from_le_bytes([b[0], b[1]]),
        None => 0,
    }
}
/// padding（offset 10-15）必须为零：head ABI 的一部分，非零即非法。
#[inline]
pub fn xh_padding(data: &[u8]) -> Option<&[u8]> {
    data.get(OFF_PADDING..PREFIX)
}
/// 指针性**从 langtype 推**，不再读 head 位。
///
/// 新 head 没有 `ptr` 字段（`xvalue_head.h`：存储形态与引用性都从 langtype 推出）。
/// 判据是 langtype 首字节为 `*`（`*T` / `**T` / `(*T)` 都算指针形态）。
/// 调用方拿不到 langtype 时应返回 false 而不是猜——本函数只做文本判定。
#[inline]
pub fn langtype_is_pointer(langtype: &str) -> bool {
    langtype.starts_with('*')
}
/// None：空字节，或固定头 bodylen=0。容量形态由 langtype 判而不在头里，所以
/// 「len=0」就是唯一可从 head 独立断定的 None 判据。
#[inline]
pub fn xh_is_none(data: &[u8]) -> bool {
    data.is_empty() || (data.len() >= PREFIX && xh_len(data) == 0)
}

/// body 切片（偏移恒 = PREFIX，长度 = head 的 bodylen）；越界 → 空。
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
/// Non-zero rc or empty output is an `Err` (passing an empty `Vec` to [`Kv::set`] would delete the key).
fn call_codec(what: &str, f: impl FnOnce(*mut *mut u8, *mut u32) -> c_int) -> Result<Vec<u8>, String> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u32 = 0;
    let rc = f(&mut out, &mut out_len);
    if rc != 0 || out.is_null() || out_len == 0 {
        if !out.is_null() {
            unsafe { free(out as *mut c_void) };
        }
        return Err(format!("kvspace codec {what} failed (rc={rc}, len={out_len})"));
    }
    let bytes = unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec();
    unsafe { free(out as *mut c_void) };
    Ok(bytes)
}

/// 固定头构造器调用（New* 的 out_len 为 u64）：成功且 total<=u32::MAX 才转 u32。
fn call_codec64(what: &str, f: impl FnOnce(*mut *mut u8, *mut u64) -> c_int) -> Result<Vec<u8>, String> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u64 = 0;
    let rc = f(&mut out, &mut out_len);
    if rc != 0 || out.is_null() || out_len == 0 || out_len > u32::MAX as u64 {
        if !out.is_null() {
            unsafe { free(out as *mut c_void) };
        }
        return Err(format!("kvspace codec {what} failed (rc={rc}, len={out_len})"));
    }
    let bytes = unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec();
    unsafe { free(out as *mut c_void) };
    Ok(bytes)
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

    /// Like [`Kv::conn`], but returns `Err` instead of panicking on connection failure.
    pub fn try_conn(dsn: &str) -> Result<Kv, String> {
        let c = CString::new(dsn).map_err(|_| format!("dsn contains NUL: {dsn:?}"))?;
        let h = unsafe { kvspaceConnect(c.as_ptr()) };
        if h.is_null() {
            return Err(format!("kvspace connect failed: {dsn}"));
        }
        Ok(Kv { h })
    }

    /// 写多对 (key, langtype, wire)：langtype 拼入值 attr 区整体单写（无 attr key）。
    /// wire 空字节 = None → 删该 key。8 字节空头是目录 marker 等合法值（len=0 非
    /// None），照常写——None 语义恒空字节（[[kvlang-layout-rust]]）。
    pub fn set(&mut self, pairs: &[(String, String, Vec<u8>)]) -> Result<(), String> {
        for (key, lt, xvalue) in pairs {
            // 末段含 ‥ 的 key 是系统变量本体：无类型（保留 codec 空 attr）。
            // 前缀可含 ‥（如 /lib/‥labels/），只拦末段。
            let last_seg = key.rsplit('/').next().unwrap_or(key);
            /* langtype 必须在**构造期**就进 wire：head 的 attrslen 与 attr 区是
             * 一次成形的。构造器拿不到类型时（系统变量本体、空 lt），保持它产的
             * 空 attr（attrslen=0）。这里只做这一次拼装，且**不带任何 attr 前缀**。 */
            let wire = if xvalue.is_empty() || lt.is_empty() || last_seg.contains('\u{2025}') {
                xvalue.clone()
            } else {
                with_langtype(xvalue, lt)
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

    /// 读 key 的完整 langtype；无该 key / 无 langtype 记录 → 空串。
    ///
    /// **直接解析 wire，不再问 `kvspaceGetHead`**（用户裁决：删掉那条 ABI——
    /// 它只是 `load` 的内部步骤，暴露出去等于给「读头」开了个可编程旁路）。
    /// wire 的 attr 区自描述：`[tag:u8][len:u16][data]`，tag 1 = langtype。
    /// 几何全部来自 head（`xh_len` / `xh_attrs_len`），不写偏移算术。
    pub fn langtype_of(&mut self, key: &str) -> String {
        let wire = self.get_one(key);
        wire_langtype(&wire).unwrap_or_default()
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

/// 空 C 串（langtype 参数的"无类型"取值：attrslen=0，attr 区零字节）。
pub static EMPTY_CSTR: [u8; 1] = [0];

/// langtype 的 C 串。空串 → [`EMPTY_CSTR`]。
///
/// 构造器**直接收 langtype**：wire 一次成形，`Kv::set` 不再二次拼 attr。此前是
/// 「构造器传空串、`Kv::set` 用 `attr_bytes` 覆写」两段拼装，head 扩容后两处各按
/// 自己的布局拼，必然对不上。
pub fn langtype_cstr(langtype: &str) -> CString {
    CString::new(langtype).unwrap_or_else(|_| CString::new("").unwrap())
}

/// 给已构造的 wire 换上 langtype：保留 head 的 bodylen/storetype/ptr 与 body，
/// 按**新布局**重写 attrslen 与 attr 区（record = `[tag][len:u16][data]`，无前缀）。
///
/// 之所以不是「拼一段前缀」：attrslen 住在 head 里，attr 区本身没有长度字段。
/// 一次成形、只写一处，才不会出现「head 说 3 字节、实际拼了 5 字节」这种裂开。
pub fn with_langtype(wire: &[u8], langtype: &str) -> Vec<u8> {
    if wire.len() < PREFIX {
        return wire.to_vec();
    }
    let bodylen = xh_len(wire) as usize;
    let Some(body_end) = PREFIX.checked_add(bodylen) else {
        return wire.to_vec();
    };
    if body_end > wire.len() {
        return wire.to_vec();
    }
    let mut out = wire[..body_end].to_vec();
    let attr_len_pos = OFF_ATTRSLEN;
    out[OFF_PADDING..PREFIX].fill(0); // padding 恒为零
    if langtype.is_empty() {
        out[attr_len_pos..attr_len_pos + 2].copy_from_slice(&0u16.to_le_bytes());
        return out;
    }
    let l = langtype.len();
    if l > usize::from(u16::MAX) - 3 {
        return wire.to_vec();
    }
    let mut attr = Vec::with_capacity(3 + l);
    attr.push(1u8); // tag = langtype
    attr.extend_from_slice(&(l as u16).to_le_bytes());
    attr.extend_from_slice(langtype.as_bytes());
    let alen = u16::try_from(attr.len()).expect("attr region fits u16");
    out[attr_len_pos..attr_len_pos + 2].copy_from_slice(&alen.to_le_bytes());
    out.extend_from_slice(&attr);
    out
}

/// 按 kind+body 产 wire（固定 8 字节 head + body + 空 attr 占位；真实类型由 Kv::set 按
/// pair 的 lt 替换）。storetype 判定对齐 durable to_wire_head：char/* 与 rwir 族→SLACK、
/// ndim>0 非容器→LARGE、其余标量→SHORT。
pub fn xvalue_to_wire(kind: &str, raw: &[u8], array_len: i32) -> Result<Vec<u8>, String> {
    let dims = al_to_dims(kind, array_len);
    if (kind.is_empty() || kind == "None") && raw.is_empty() {
        return call_codec64("HeadNewNone", |out, out_len| unsafe {
            kvspaceXvalueHeadNewNone(out, out_len)
        });
    }
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    if kind == "char" || kind.starts_with("char/") {
        return call_codec64("HeadNewSlack", |out, out_len| unsafe {
            kvspaceXvalueHeadNewSlack(
                raw.as_ptr(),
                raw.len() as u64,
                empty,
                out,
                out_len,
            )
        });
    }
    if !dims.is_empty() && !kind.contains('·') && !kind.starts_with('/') {
        let d: Vec<u64> = dims.iter().map(|&x| x as u64).collect();
        return call_codec64("HeadNewTensor", |out, out_len| unsafe {
            kvspaceXvalueHeadNewTensor(d.as_ptr(), d.len() as u32, raw.as_ptr(), raw.len() as u64, empty, out, out_len)
        });
    }
    if kind == "rwir" || kind == "def langtype" || (kind == "rwfunc" && !raw.is_empty()) {
        return call_codec64("HeadNewCode", |out, out_len| unsafe {
            kvspaceXvalueHeadNewCode(
                raw.as_ptr(),
                raw.len() as u64,
                empty,
                out,
                out_len,
            )
        });
    }
    call_codec64("HeadNewScalar", |out, out_len| unsafe {
        kvspaceXvalueHeadNewScalar(raw.as_ptr(), raw.len() as u32, empty, out, out_len)
    })
}

/// 按 wire 字节原位填 head 视图（16 字节固定头）。
/// langtype 不在 head 的结构体视图里——调用方若已知道类型可传入；否则留空，
/// 由 `langtype_from_wire` 从 attr 区取。
#[inline]
fn xh_padding_is_zero(data: &[u8]) -> bool {
    xh_padding(data).is_some_and(|p| p.iter().all(|&byte| byte == 0))
}

/// 从一条 wire 里取 langtype 文本（attr tag 1）；没有该记录 → None。
///
/// attr 区自描述：`[tag:u8][len:u16][data]`，紧跟 body 之后，长度由 head 的
/// `attrslen` 给出（**没有**独立长度前缀）。几何全部取自 head，不写偏移算术。
pub fn wire_langtype(wire: &[u8]) -> Option<String> {
    if wire.len() < PREFIX || !xh_padding_is_zero(wire) {
        return None;
    }
    let body_len = xh_len(wire) as usize;
    let attrs_len = xh_attrs_len(wire) as usize;
    let start = PREFIX.checked_add(body_len)?;
    let end = start.checked_add(attrs_len)?;
    if end > wire.len() {
        return None;
    }
    let mut at = start;
    while at + 3 <= end {
        let tag = wire[at];
        let rlen = u16::from_le_bytes([wire[at + 1], wire[at + 2]]) as usize;
        let data_start = at + 3;
        let data_end = data_start.checked_add(rlen)?;
        if data_end > end {
            return None;
        }
        if tag == 1 {
            return Some(String::from_utf8_lossy(&wire[data_start..data_end]).into_owned());
        }
        at = data_end;
    }
    None
}

// ── 标准标量构造器 ───────────────────────────────────────────────────

pub fn new_char(kind: &str, s: &str) -> Result<Vec<u8>, String> {
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
    call_codec64("HeadNewSlack", |out, out_len| unsafe {
        kvspaceXvalueHeadNewSlack(
            raw.as_ptr(),
            raw.len() as u64,
            empty,
            out,
            out_len,
        )
    })
}

pub fn new_char_byte(bytes: &[u8]) -> Result<Vec<u8>, String> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec("NewChar", |out, out_len| unsafe {
        kvspaceNewChar(bytes.as_ptr(), bytes.len() as u32, empty, out, out_len)
    })
}

/// 空值字面量 `None` 的 wire：长度 0 的短内联头（空 body、无 langtype，16 字节）。
///
/// **不是零字节**——存储层以 `invalid XValue` 拒绝零字节值（实测见
/// `workspace/tmp/coding1/none_wire_probe.c`）。也不要手拼这 16 字节：
/// 头布局的唯一来源是 kvspace 的 `kvspaceXvalueHeadNewNone`。
pub fn new_none() -> Result<Vec<u8>, String> {
    call_codec64("HeadNewNone", |out, out_len| unsafe {
        kvspaceXvalueHeadNewNone(out, out_len)
    })
}

pub fn new_bool(v: bool) -> Result<Vec<u8>, String> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec("NewBool", |out, out_len| unsafe { kvspaceNewBool(v as u8, empty, out, out_len) })
}

pub fn new_int64(v: i64) -> Result<Vec<u8>, String> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec("NewInt64", |out, out_len| unsafe { kvspaceNewInt64(v, empty, out, out_len) })
}

pub fn new_float64(v: f64) -> Result<Vec<u8>, String> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec("NewFloat64", |out, out_len| unsafe { kvspaceNewFloat64(v, empty, out, out_len) })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Codec failure must return `Err` rather than an empty `Vec` (which `Kv::set` treats as key deletion).
    #[test]
    fn codec_failure_is_error_not_empty_value() {
        assert!(call_codec("t", |_, _| -1).is_err());
        assert!(call_codec64("t", |_, _| -1).is_err());
        // `rc == 0` with empty output is also an error.
        assert!(call_codec("t", |_, _| 0).is_err());
        assert!(call_codec64("t", |_, _| 0).is_err());
        let v = new_int64(7).unwrap();
        assert_eq!(xh_body_slice(&v), 7i64.to_le_bytes());
    }
}
