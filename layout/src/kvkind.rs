//! kvlang 自有 kind（rwir/rwfunc/scope）+ XValue TLV 字节访问器。
//!
//! 标准 kind（char/utf8、int64、bool、index…）由 kvspace-durable 编解码；
//! rwir/rwfunc/scope 是 kvlang 自己的 kind，body 格式由本模块定义，
//! head 复用 kvspace-durable 暴露的 TLV head（见 [`super::ffi`]）。

use super::ffi;

// ── kind 常量 ─────────────────────────────────────────────────────────

pub const KIND_CHAR: &str = "char/utf32";
pub const KIND_INDEX: &str = "index";
pub const KIND_STRUCT: &str = "struct";

// kvlang 自有 kind
pub const KIND_RWIR: &str = "rwir";
pub const KIND_RWFUNC: &str = "rwfunc";
pub const KIND_DEF_RWIR: &str = "def rwir";
pub const KIND_DEF_LANGTYPE: &str = "def langtype";
pub const KIND_RWIR_OR_RWFUNC: &str = "rwir|rwfunc";

// ── 通用 XValue 字节访问器 ───────────────────────────────────────────

/// 空字节 = None。
pub fn is_none(data: &[u8]) -> bool {
    data.is_empty()
}

pub fn head(data: &[u8]) -> ffi::kvspaceHead_t {
    ffi::decode_head(data)
}

/// 解析 langtype 内容 → (dims, base kind)。langtype 无前缀（ref/ptr 归 head.ref）。
/// **map langtype 无形状段**：`{keylt}·{valt}` 里 `·` 之前的方括号是键类型（`[int64]`、
/// `[float64,float64]`），不是维度——与 `[2]float64`（数组形状）截然不同，故整串即基 kind。
/// 非 map 串也只把**纯数字/空/?**的方括号当形状（见 [[map容器]]）。
pub fn parse_langtype(kx: &str) -> (Vec<i32>, String) {
    if kx.contains(super::keytree::MEMBER_SEP) {
        return (Vec::new(), kx.to_string());
    }
    if kx.starts_with('[') {
        match kx.find(']') {
            Some(end) => {
                let inner = &kx[1..end];
                if inner.split(',').all(|d| {
                    let d = d.trim();
                    d.is_empty() || d == "?" || d.parse::<i32>().is_ok()
                }) {
                    return (
                        inner
                            .split(',')
                            .filter(|d| !d.is_empty())
                            .map(|d| d.parse().unwrap_or(0))
                            .collect(),
                        kx[end + 1..].to_string(),
                    );
                }
                (Vec::new(), kx.to_string())
            }
            None => (Vec::new(), kx.to_string()),
        }
    } else {
        (Vec::new(), kx.to_string())
    }
}

/// 读 head 的 langtype 内容（去 NUL）。
pub fn langtype(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    let h = ffi::decode_head(data);
    let end = h
        .langtype
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(h.langtype.len());
    String::from_utf8_lossy(&h.langtype[..end]).into_owned()
}

pub fn kind(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    parse_langtype(&langtype(data)).1
}

pub fn is_ptr(data: &[u8]) -> bool {
    !data.is_empty() && ffi::decode_head(data).r#ref == 1
}

pub fn array_len(data: &[u8]) -> i32 {
    if data.is_empty() {
        return 0;
    }
    let dims = parse_langtype(&langtype(data)).0;
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

/// 指针目标 key（Ptr 的 body 即目标 key 路径；head 去 * 为目标完整 langtype）。
pub fn ptr_target(data: &[u8]) -> String {
    let h = ffi::decode_head(data);
    String::from_utf8_lossy(body(data, &h)).into_owned()
}

/// char/utf8 的明文表示（body 字节按 UTF-8 解码）。
pub fn value_string(data: &[u8]) -> String {
    let h = ffi::decode_head(data);
    String::from_utf8_lossy(body(data, &h)).into_owned()
}

/// 把 XValue TLV 解码成可读文本，形如 "kind:value"（指针 "→target:kind"、空 "None"）。
/// 对齐 kvspace CLI 的 format_value/plain，供 dump 审查 lower 后的 /lib。
pub fn display(data: &[u8]) -> String {
    if data.is_empty() {
        return "None".to_string();
    }
    let (_, k) = parse_langtype(&langtype(data));
    if k.is_empty() {
        return "None".to_string();
    }
    let h = ffi::decode_head(data);
    let b = body(data, &h);
    if h.r#ref == 1 {
        return format!("→{}:{}", String::from_utf8_lossy(b), k);
    }
    format!("{}:{}", k, plain_value(&k, b))
}

fn le_u32(b: &[u8]) -> u32 {
    b.iter()
        .take(4)
        .enumerate()
        .fold(0, |a, (i, &x)| a | ((x as u32) << (8 * i)))
}
fn le_u64(b: &[u8]) -> u64 {
    b.iter()
        .take(8)
        .enumerate()
        .fold(0u64, |a, (i, &x)| a | ((x as u64) << (8 * i)))
}
fn fmt_float(v: f64) -> String {
    let s = format!("{v}");
    if s.contains('.') {
        s
    } else {
        format!("{s}.0")
    }
}
fn count_names(b: &[u8]) -> usize {
    if b.len() < 4 {
        0
    } else {
        le_u32(b) as usize
    }
}
fn arr<const N: usize>(b: &[u8]) -> [u8; N] {
    let mut a = [0u8; N];
    let n = b.len().min(N);
    a[..n].copy_from_slice(&b[..n]);
    a
}

fn plain_value(k: &str, b: &[u8]) -> String {
    match k {
        "bool" => (b.first().copied().unwrap_or(0) != 0).to_string(),
        "int8" => (b.first().map(|&x| x as i8).unwrap_or(0) as i64).to_string(),
        "int16" => (i16::from_le_bytes(arr(b)) as i64).to_string(),
        "int32" => (i32::from_le_bytes(arr(b)) as i64).to_string(),
        "int64" => i64::from_le_bytes(arr(b)).to_string(),
        "uint8" => b.first().copied().unwrap_or(0).to_string(),
        "uint16" => u16::from_le_bytes(arr(b)).to_string(),
        "uint32" => le_u32(b).to_string(),
        "uint64" => le_u64(b).to_string(),
        "float32" => fmt_float(f32::from_le_bytes(arr(b)) as f64),
        "float64" => fmt_float(f64::from_le_bytes(arr(b))),
        "char/utf8" | "char/ascii" => String::from_utf8_lossy(b).into_owned(),
        "char/utf32" => b
            .chunks(4)
            .map(|c| char::from_u32(le_u32(c)).unwrap_or('\u{FFFD}'))
            .collect(),
        "index" => format!("({})", count_names(b)),
        // kvlang 自有 kind：body = [2B nr][2B nw][1B dynamic][sig]；槽值/调用目标 nr=nw=0，取 sig 即可。
        "rwir" | "rwir|rwfunc" | "rwfunc" | "def rwir" => {
            let (nr, nw, dynamic) = if b.len() >= 5 {
                (
                    u16::from_le_bytes([b[0], b[1]]),
                    u16::from_le_bytes([b[2], b[3]]),
                    b[4] != 0,
                )
            } else {
                (0, 0, false)
            };
            let sig = String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned();
            let var = if dynamic { "..." } else { "" };
            if nr == 0 && nw == 0 {
                sig
            } else {
                format!("(nr={nr},nw={nw}{var}) {sig}")
            }
        }
        _ => String::from_utf8_lossy(b).into_owned(),
    }
}

pub fn is_char_kind(k: &str) -> bool {
    k.starts_with("char/")
}

// ── kvlang 自有 kind：rwir / def rwir / def langtype ─────────────────
//
// 铁律：任何 rwir/rwfunc 值的 body 只记计数头 [2B nr LE][2B nw LE][1B dynamic]，
// 禁止携带具体参数。指令槽 [n,x] 的引用串（opcode/操作数名）是「每坐标一个值」，
// 落于 rwir 槽值 body 的尾部载荷；定义（rwfunc/def rwir）的各参数类型分散落在
// 签名行 [0,x] 槽（各为一个 def langtype 值，body=该参数 langtype 串）。
// dynamic=末读参变参（arity，非 langtype）。

/// 计数头 [nr:u16 LE][nw:u16 LE][dynamic:u8]（定义体，无参数载荷）。
fn counts_body(nr: i32, nw: i32, dynamic: bool) -> Vec<u8> {
    let mut raw = Vec::with_capacity(5);
    raw.extend_from_slice(&(nr as u16).to_le_bytes());
    raw.extend_from_slice(&(nw as u16).to_le_bytes());
    raw.push(dynamic as u8);
    raw
}

/// 指令槽值：计数头 + 单个引用串载荷（opcode/操作数名，每坐标一个值）。
fn rwir_slot_body(sig: &str) -> Vec<u8> {
    let mut raw = counts_body(0, 0, false);
    raw.extend_from_slice(sig.as_bytes());
    raw
}

pub fn new_rwir(nr: i32, nw: i32, sig: &str) -> Vec<u8> {
    let mut raw = counts_body(nr, nw, false);
    raw.extend_from_slice(sig.as_bytes());
    ffi::tlv_encode(KIND_RWIR, &raw, 1)
}

/// 调用目标（看起来像函数调用的 opcode）→ langtype `rwir|rwfunc` 并列。
/// 静态无法判定是扩展 rwir 还是用户 rwfunc，交 runtime 查 /lib/<op> 的 XValue kind 分派。
pub fn new_rwir_union(sig: &str) -> Vec<u8> {
    ffi::tlv_encode(KIND_RWIR_OR_RWFUNC, &rwir_slot_body(sig), 1)
}

/// def rwir 路由头：仅计数头，无参数载荷。各参数落 [0,x] 签名行槽（def langtype）。
pub fn new_defrwir(nr: i32, nw: i32, dynamic: bool) -> Vec<u8> {
    ffi::tlv_encode(KIND_DEF_RWIR, &counts_body(nr, nw, dynamic), 1)
}

/// 签名行 [0,x] 槽：一个参数的类型定义，body=该参数完整 langtype 串。
pub fn new_def_langtype(langtype: &str) -> Vec<u8> {
    ffi::tlv_encode(KIND_DEF_LANGTYPE, langtype.as_bytes(), 1)
}

/// rwfunc 参数定义键（点后缀 .[0,-k]）：body=名字\x00类型串，langtype=def langtype。
pub fn new_def_param(name: &str, langtype: &str) -> Vec<u8> {
    let mut body = name.as_bytes().to_vec();
    body.push(0);
    body.extend_from_slice(langtype.as_bytes());
    ffi::tlv_encode(KIND_DEF_LANGTYPE, &body, 1)
}

/// 解析参数定义键 body（名字\x00类型串）→ (名字, 类型)。
pub fn def_param_parts(data: &[u8]) -> Option<(String, String)> {
    if data.is_empty() {
        return None;
    }
    let h = ffi::decode_head(data);
    let b = body(data, &h);
    let nul = b.iter().position(|&x| x == 0)?;
    let name = String::from_utf8_lossy(&b[..nul]).into_owned();
    let ty = String::from_utf8_lossy(&b[nul + 1..]).into_owned();
    Some((name, ty))
}

// ── struct 原型（对齐 runtime kvlangBuiltinMemindex）─────────────────
//
// /lib/Name       kind=struct，body="name:langtype\n..."（字段声明类型，供实例化类型校验）
// /lib/Name·      kind=index，body=[4B count LE][name\n...]（字段名唯一权威）

pub fn new_struct(fields: &[(String, String)]) -> Vec<u8> {
    let body = fields
        .iter()
        .map(|(n, t)| format!("{n}:{t}"))
        .collect::<Vec<_>>()
        .join("\n");
    ffi::tlv_encode(KIND_STRUCT, body.as_bytes(), 1)
}

pub fn new_memindex(names: &[String]) -> Vec<u8> {
    let mut raw = (names.len() as u32).to_le_bytes().to_vec();
    raw.extend_from_slice(names.join("\n").as_bytes());
    ffi::tlv_encode(KIND_INDEX, &raw, 1)
}

// ── kvlang 自有 kind：rwfunc ────────────────────────────────────────
//
// body = 计数头 [2B nr LE][2B nw LE][1B dynamic]，array_len=num_insts。
// 各参数类型落签名行 [0,x] 槽（def langtype），不入 body（见上「铁律」）。

pub fn new_rwfunc(num_insts: i32, nr: i32, nw: i32, dynamic: bool) -> Vec<u8> {
    ffi::tlv_encode(KIND_RWFUNC, &counts_body(nr, nw, dynamic), num_insts)
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

/// 主槽计数头 (nr, nw, dynamic)——直接吃整条 XValue（rwfunc/def rwir）。
pub fn counts(data: &[u8]) -> (i32, i32, bool) {
    if data.is_empty() {
        return (0, 0, false);
    }
    let h = ffi::decode_head(data);
    let b = body(data, &h);
    if b.len() >= 5 {
        (
            u16::from_le_bytes([b[0], b[1]]) as i32,
            u16::from_le_bytes([b[2], b[3]]) as i32,
            b[4] != 0,
        )
    } else {
        (0, 0, false)
    }
}

/// 字面量的明文源值（不含 kind 前缀、不含引号）；char/utf32 按码点正确解码。
pub fn plain(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    let (_, k) = parse_langtype(&langtype(data));
    let h = ffi::decode_head(data);
    plain_value(&k, body(data, &h))
}

/// rwir 族槽值的载荷串（opcode / 引用名）：body 去 5 字节计数头后的字节。
/// 写槽值 → (名字, 声明类型)：写目标带 map langtype 标注（`x:{keylt}·{valt} = {}`）时，槽的
/// langtype 即该 map langtype、body 即变量名（见 [[map容器]]）；其余写槽走 rwir 引用载荷
/// （跳过 5B 计数头）。dump 逆向时据此还原 `-> x:<type>`，使 round-trip 不丢容器类型。
pub fn write_slot_name(data: &[u8]) -> (String, String) {
    if data.is_empty() {
        return (String::new(), String::new());
    }
    let k = kind(data);
    if k.contains(super::keytree::MEMBER_SEP) {
        return (value_string(data), k);
    }
    (rwir_sig(data), String::new())
}

pub fn rwir_sig(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    let h = ffi::decode_head(data);
    let b = body(data, &h);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}
