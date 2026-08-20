//! kvlang 自有 kind（rwir/rwfunc/scope）+ XValue TLV 字节访问器。
//!
//! 标准 kind（char/utf8、int64、bool、index…）由 kvspace-durable 编解码；
//! rwir/rwfunc/scope 是 kvlang 自己的 kind，body 格式由本模块定义，
//! head 复用 kvspace-durable 暴露的 TLV head（见 [`super::ffi`]）。

use super::ffi;

// ── kind 常量 ─────────────────────────────────────────────────────────

pub const KIND_CHAR: &str = "char/utf32";
pub const KIND_CHAR_UTF8: &str = "char/utf8";
pub const KIND_CHAR_ASCII: &str = "char/ascii";
pub const KIND_BOOL: &str = "bool";
pub const KIND_INT64: &str = "int64";
pub const KIND_FLOAT64: &str = "float64";
pub const KIND_OBJ: &str = "obj";
pub const KIND_MAP: &str = "map";
pub const KIND_INDEX: &str = "index";
pub const KIND_EXT_INDEX: &str = "extindex";

// kvlang 自有 kind
pub const KIND_RWIR: &str = "rwir";
pub const KIND_RWFUNC: &str = "rwfunc";
pub const KIND_SCOPE: &str = "scope";

// ── 通用 XValue 字节访问器 ───────────────────────────────────────────

/// 空字节 = None。
pub fn is_none(data: &[u8]) -> bool {
    data.is_empty()
}

pub fn head(data: &[u8]) -> ffi::kvspaceHead_t {
    ffi::decode_head(data)
}

pub fn kind(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    let h = ffi::decode_head(data);
    let end = h.kind.iter().position(|&b| b == 0).unwrap_or(h.kind.len());
    String::from_utf8_lossy(&h.kind[..end]).into_owned()
}

pub fn is_ptr(data: &[u8]) -> bool {
    !data.is_empty() && ffi::decode_head(data).is_ptr != 0
}

pub fn array_len(data: &[u8]) -> i32 {
    if data.is_empty() {
        return 0;
    }
    ffi::decode_head(data).array_len
}

/// 从 data 截取 body 字节。
pub fn body<'a>(data: &'a [u8], h: &ffi::kvspaceHead_t) -> &'a [u8] {
    let off = h.body_offset as usize;
    let len = h.body_len as usize;
    if off + len > data.len() {
        return &[];
    }
    &data[off..off + len]
}

/// 软链接目标（Ptr 的 body 即目标路径）。
pub fn ptr_target(data: &[u8]) -> String {
    let h = ffi::decode_head(data);
    String::from_utf8_lossy(body(data, &h)).into_owned()
}

/// char/utf8 的明文表示（body 字节按 UTF-8 解码）。
pub fn value_string(data: &[u8]) -> String {
    let h = ffi::decode_head(data);
    String::from_utf8_lossy(body(data, &h)).into_owned()
}

pub fn is_char_kind(k: &str) -> bool {
    k.starts_with("char/")
}

// ── kvlang 自有 kind：rwir ──────────────────────────────────────────
//
// body = [2B nr LE][2B nw LE][sig]，array_len=1。

pub fn new_rwir(nr: i32, nw: i32, sig: &str) -> Vec<u8> {
    let mut raw = Vec::with_capacity(4 + sig.len());
    raw.extend_from_slice(&(nr as u16).to_le_bytes());
    raw.extend_from_slice(&(nw as u16).to_le_bytes());
    raw.extend_from_slice(sig.as_bytes());
    ffi::tlv_encode(KIND_RWIR, &raw, 1)
}

// ── kvlang 自有 kind：rwfunc ────────────────────────────────────────
//
// body = [2B nr LE][2B nw LE][param_types 以 \n 连接]，array_len=num_insts。

pub fn new_rwfunc(num_insts: i32, nr: i32, nw: i32, param_types: &[String]) -> Vec<u8> {
    let mut raw = Vec::with_capacity(4 + param_types.iter().map(|s| s.len()).sum::<usize>());
    raw.extend_from_slice(&(nr as u16).to_le_bytes());
    raw.extend_from_slice(&(nw as u16).to_le_bytes());
    raw.extend_from_slice(param_types.join("\n").as_bytes());
    ffi::tlv_encode(KIND_RWFUNC, &raw, num_insts)
}

/// rwfunc body 访问器（layout 读回签名时用）。
pub fn rwfunc_num_reads(body: &[u8]) -> i32 {
    if body.len() < 2 {
        return 0;
    }
    u16::from_le_bytes([body[0], body[1]]) as i32
}

pub fn rwfunc_num_writes(body: &[u8]) -> i32 {
    if body.len() < 4 {
        return 0;
    }
    u16::from_le_bytes([body[2], body[3]]) as i32
}

pub fn rwfunc_param_types(body: &[u8]) -> Vec<String> {
    if body.len() <= 4 {
        return Vec::new();
    }
    String::from_utf8_lossy(&body[4..])
        .split('\n')
        .map(|s| s.to_string())
        .collect()
}
