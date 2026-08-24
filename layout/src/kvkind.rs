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
pub const KIND_OBJ: &str = "objindex";
pub const KIND_MAP: &str = "strkeymapindex";
pub const KIND_INDEX: &str = "index";
pub const KIND_EXT_INDEX: &str = "extindex";

// kvlang 自有 kind
pub const KIND_RWIR: &str = "rwir";
pub const KIND_RWFUNC: &str = "rwfunc";
pub const KIND_DEF_RWIR: &str = "defrwir";
pub const KIND_DEF_RWFUNC: &str = "defrwfunc";
pub const KIND_RWIR_OR_RWFUNC: &str = "rwir|rwfunc";
pub const KIND_SCOPE: &str = "scope";

// ── 通用 XValue 字节访问器 ───────────────────────────────────────────

/// 空字节 = None。
pub fn is_none(data: &[u8]) -> bool {
    data.is_empty()
}

pub fn head(data: &[u8]) -> ffi::kvspaceHead_t {
    ffi::decode_head(data)
}

/// 解析 head 的 kindexpr 内容 → (ref, dims, base kind)。
pub fn parse_kindexpr(kx: &str) -> (i32, Vec<i32>, String) {
    let (r, rest) = match kx.as_bytes().first() {
        Some(b'*') => (1, &kx[1..]),
        Some(b'@') => (2, &kx[1..]),
        _ => (0, kx),
    };
    if rest.starts_with('[') {
        match rest.find(']') {
            Some(end) => (
                r,
                rest[1..end]
                    .split(',')
                    .filter(|d| !d.is_empty())
                    .map(|d| d.parse().unwrap_or(0))
                    .collect(),
                rest[end + 1..].to_string(),
            ),
            None => (r, Vec::new(), rest.to_string()),
        }
    } else {
        (r, Vec::new(), rest.to_string())
    }
}

/// 读 head 的 kindexpr 内容（去 NUL）。
pub fn kindexpr(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    let h = ffi::decode_head(data);
    let end = h.kindexpr.iter().position(|&b| b == 0).unwrap_or(h.kindexpr.len());
    String::from_utf8_lossy(&h.kindexpr[..end]).into_owned()
}

pub fn kind(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    parse_kindexpr(&kindexpr(data)).2
}

pub fn is_ptr(data: &[u8]) -> bool {
    !data.is_empty() && parse_kindexpr(&kindexpr(data)).0 == 1
}

pub fn array_len(data: &[u8]) -> i32 {
    if data.is_empty() {
        return 0;
    }
    let dims = parse_kindexpr(&kindexpr(data)).1;
    if dims.is_empty() {
        1
    } else {
        dims.iter().product()
    }
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

// ── kvlang 自有 kind：rwir / defrwir ────────────────────────────────
//
// body = [2B nr LE][2B nw LE][sig]，array_len=1。
// rwir=槽值（引用串/opcode），defrwir=定义（签名）。

fn rwir_body(nr: i32, nw: i32, sig: &str) -> Vec<u8> {
    let mut raw = Vec::with_capacity(4 + sig.len());
    raw.extend_from_slice(&(nr as u16).to_le_bytes());
    raw.extend_from_slice(&(nw as u16).to_le_bytes());
    raw.extend_from_slice(sig.as_bytes());
    raw
}

pub fn new_rwir(nr: i32, nw: i32, sig: &str) -> Vec<u8> {
    ffi::tlv_encode(KIND_RWIR, &rwir_body(nr, nw, sig), 1)
}

/// 调用目标（看起来像函数调用的 opcode）→ kindexpr `rwir|rwfunc` 并列。
/// 静态无法判定是扩展 rwir 还是用户 rwfunc，交 runtime 查 /lib/<op> 的 XValue kind 分派。
pub fn new_rwir_union(sig: &str) -> Vec<u8> {
    ffi::tlv_encode(KIND_RWIR_OR_RWFUNC, &rwir_body(0, 0, sig), 1)
}

pub fn new_defrwir(nr: i32, nw: i32, sig: &str) -> Vec<u8> {
    ffi::tlv_encode(KIND_DEF_RWIR, &rwir_body(nr, nw, sig), 1)
}

// ── kvlang 自有 kind：rwfunc ────────────────────────────────────────
//
// body = [2B nr LE][2B nw LE][param_types 以 \n 连接]，array_len=num_insts。

pub fn new_rwfunc(num_insts: i32, nr: i32, nw: i32, param_types: &[String]) -> Vec<u8> {
    let mut raw = Vec::with_capacity(4 + param_types.iter().map(|s| s.len()).sum::<usize>());
    raw.extend_from_slice(&(nr as u16).to_le_bytes());
    raw.extend_from_slice(&(nw as u16).to_le_bytes());
    raw.extend_from_slice(param_types.join("\n").as_bytes());
    ffi::tlv_encode(KIND_DEF_RWFUNC, &raw, num_insts)
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
