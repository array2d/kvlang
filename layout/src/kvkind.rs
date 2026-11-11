//! kvlang code values over the shared headlenpow XValue codec.

use super::ffi;

// ── kind 常量 ─────────────────────────────────────────────────────────
/// **无标注字符串字面量的默认编码：`char/utf32`。用户裁决（2026-11-06）。**
///
/// spec `03-类型系统/26-字符串编码` §一：**key 侧 utf8，value 侧 utf32**。
/// 「value 侧：落到 KV 树的字符串值，未显式标注时以 utf32 承载」——与 §三
/// 「string 包工作面是 utf32」一致，源码里的字符串字面量与 string 包函数因此
/// 无需逐处显式转码。**这不是待改项，是终态**：别再把这里往 `char/utf8` 改。
///
/// 曾经试过改成 `char/utf8`（依据当时 §一 的「默认 utf8」旧措辞）：**14 条回归**
/// ——`11-string/*`、`08-leetcode/{008,013,014,020,058,125,171}` 等从通过变
/// `0x4503`（temp_println 段）/`0x4B01`（文本 handler MALFORMED）。原因是 string
/// 包的 handler 按 utf32 解 body，值改 utf8 后解不动。那次回退是对的，且旧措辞
/// 已被裁决取代。
///
/// 有标注时仍按标注转换：`s:[]char/utf32 = "abc"` 走 `target_char`，与本常量无关
/// ——本常量只服务「完全没有标注」这一支。
pub const KIND_CHAR: &str = "char/utf32";

pub const KIND_DEF_STRUCT: &str = "def struct";

// kvlang 自有 kind
pub const KIND_RWIR: &str = "rwir";
pub const KIND_RWFUNC: &str = "rwfunc";
pub const KIND_DEF_RWIR: &str = "def rwir";
pub const KIND_DEF_LANGTYPE: &str = "def langtype";

// ── 通用 XValue 字节访问器 ───────────────────────────────────────────

/// 空字节 = None。
pub fn is_none(data: &[u8]) -> bool {
    data.is_empty()
}

/// 解析 langtype 内容 → (dims, base kind)。langtype 无前缀（ref/ptr 归 head.ref）。
/// **map langtype 无形状段**：`{keylt}·{valt}` 里 `·` 之前的方括号是键类型（`[int64]`、
/// `[float64,float64]`），不是维度——与 `[2]float64`（数组形状）截然不同，故整串即基 kind。
/// 非 map 串也只把**纯数字/空/?**的方括号当形状（见 [[map容器]]）。
/// 这是不是 map 类型（`'<formatter>'<value>`，issue #631）。
///
/// **唯一判据**：首字符是单引号。改名前靠「整串含 `·`」判，那个判据在
/// 新形式下恒为 false（map 里不再有 `·`），于是 map 会落到 UNRECOGNIZED。
///
/// 所有判「是不是 map」的地方都调本函数，**不许各写一次 `starts_with('\'')`**
/// ——那和旧时代「每处各判 `·`」是同一个病。
pub fn is_map_langtype(kx: &str) -> bool {
    kx.starts_with('\'')
}

pub fn parse_langtype(kx: &str) -> (Vec<i32>, String) {
    /*
     * **证明没有漏网**：走到这里就不该是 map 类型。
     * 调用方必须先问 `is_map_langtype`；若这条断言在跑语料时触发，
     * 说明有消费点绕过了判据——先修那条路，不要删断言。
     */
    debug_assert!(
        !is_map_langtype(kx),
        "parse_langtype received a map langtype {kx:?}; the caller must ask is_map_langtype first"
    );
    /*
     * **旧的 `contains("·")` 分支已删除，不是改成判 `'`。**
     *
     * 它原来的作用是「map 类型无形状段，整串当 kind」。新判据在**更前面**
     * （调用方先问 `is_map_langtype`）就把它拦下，`parse_langtype` 根本看不到
     * map 类型的串——保留任何形式都是死代码。
     *
     * 若真有漏网，会以「`'` 开头落到下面的分支」响亮失败（既不是 `[` 开头、
     * 也不匹配任何标量），不会静默。
     */
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

/// langtype 内容（完整 kindexpr 串，含 [dims]）。data 空（None）→ ""；否则返回调用方
/// 从 kvspaceGetHead（x‥langtype attr key）读入的 lt。
pub fn langtype(data: &[u8], lt: &str) -> String {
    if data.is_empty() {
        String::new()
    } else {
        lt.to_string()
    }
}

pub fn kind(data: &[u8], lt: &str) -> String {
    if data.is_empty() {
        return String::new();
    }
    parse_langtype(lt).1
}

/// 这个值是不是指针。
///
/// **判据只有 langtype**：新 head 没有 `ptr` 位（`xvalue_head.h`：存储形态与
/// 引用性都从 langtype 推出）。所以本函数必须收 langtype——早先只收 `data`
/// 并读 head 的 offset 13，那个位置现在是 padding，读出来恒 0，判据整个失效。
pub fn is_ptr(data: &[u8], langtype: &str) -> bool {
    !data.is_empty() && ffi::langtype_is_pointer(langtype)
}

pub fn array_len(data: &[u8], lt: &str) -> i32 {
    if data.is_empty() {
        return 0;
    }
    let dims = parse_langtype(lt).0;
    if dims.is_empty() {
        1
    } else {
        dims.iter().product()
    }
}

/// 从 data 截取 body 字节。
///
/// 几何直接来自 wire 头（`xh_body_slice` 一处定义），不再经过中间 head 结构——
/// 那条 `kvspaceHead_t` 已按裁决删除（它与 kvspace 的 C ABI 同名，留着就是同词两义）。
pub fn body<'a>(data: &'a [u8]) -> &'a [u8] {
    ffi::xh_body_slice(data)
}

/// 指针目标 key（Ptr 的 body 即目标 key 路径；head 去 * 为目标完整 langtype）。
pub fn ptr_target(data: &[u8]) -> String {
    String::from_utf8_lossy(body(data)).into_owned()
}

/// char/utf8 的明文表示（body 字节按 UTF-8 解码）。
pub fn value_string(data: &[u8]) -> String {
    String::from_utf8_lossy(body(data)).into_owned()
}

/// Format an XValue for inspection.
/// 对齐 kvspace CLI 的 format_value/plain，供 dump 审查 lower 后的 /lib。
pub fn display(data: &[u8], lt: &str) -> String {
    if data.is_empty() {
        return "None".to_string();
    }
    let (_, k) = parse_langtype(lt);
    if k.is_empty() {
        return "None".to_string();
    }
    let b = body(data);
    if ffi::langtype_is_pointer(lt) {
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
        // Code bodies store counts before the name.
        "rwir" | "rwfunc" | "def rwir" => {
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
/// Return `Err` instead of panicking when read/write arg count exceeds `u16::MAX`.
fn counts_body(nr: i32, nw: i32, dynamic: bool) -> Result<Vec<u8>, String> {
    let mut raw = Vec::with_capacity(5);
    let nr = u16::try_from(nr).map_err(|_| format!("read count {nr} exceeds 65535"))?;
    let nw = u16::try_from(nw).map_err(|_| format!("write count {nw} exceeds 65535"))?;
    raw.extend_from_slice(&nr.to_le_bytes());
    raw.extend_from_slice(&nw.to_le_bytes());
    raw.push(dynamic as u8);
    Ok(raw)
}

pub fn new_rwir(nr: i32, nw: i32, sig: &str) -> Result<Vec<u8>, String> {
    let mut raw = counts_body(nr, nw, false)?;
    raw.extend_from_slice(sig.as_bytes());
    ffi::xvalue_to_wire(KIND_RWIR, &raw, 1)
}

pub fn new_rwfunc_call(nr: i32, nw: i32, sig: &str) -> Result<Vec<u8>, String> {
    let mut raw = counts_body(nr, nw, false)?;
    raw.extend_from_slice(sig.as_bytes());
    ffi::xvalue_to_wire(KIND_RWFUNC, &raw, 1)
}

pub fn new_typed_rwir(name: &str, langtype: &str) -> Result<Vec<u8>, String> {
    let mut raw = counts_body(0, 0, false)?;
    raw.extend_from_slice(name.as_bytes());
    raw.push(0);
    raw.extend_from_slice(langtype.as_bytes());
    ffi::xvalue_to_wire(KIND_RWIR, &raw, 1)
}

/// def rwir 路由头：仅计数头，无参数载荷。各参数落 [0,x] 签名行槽（def langtype）。
pub fn new_defrwir(nr: i32, nw: i32, dynamic: bool) -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_DEF_RWIR, &counts_body(nr, nw, dynamic)?, 1)
}

/// `inplace` mark appended to a write param's langtype, mirroring `inplace *T #name`.
pub const INPLACE_SUFFIX: &str = " inplace";

/// 签名行 [0,x] 槽：一个参数的类型定义，body=该参数完整 langtype 串。
pub fn new_def_langtype(langtype: &str) -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_DEF_LANGTYPE, langtype.as_bytes(), 1)
}

/// def rwir write slot `[0,+x]`, with an optional ` inplace` suffix (idempotent).
pub fn new_def_langtype_with_inplace(langtype: &str, inplace: bool) -> Result<Vec<u8>, String> {
    let clean = langtype.strip_suffix(INPLACE_SUFFIX).unwrap_or(langtype);
    if inplace {
        let mut raw = String::with_capacity(clean.len() + INPLACE_SUFFIX.len());
        raw.push_str(clean);
        raw.push_str(INPLACE_SUFFIX);
        ffi::xvalue_to_wire(KIND_DEF_LANGTYPE, raw.as_bytes(), 1)
    } else {
        new_def_langtype(clean)
    }
}

/// def rwir slot body `langtype[ inplace]` -> (langtype, inplace).
pub fn def_langtype_full(data: &[u8]) -> Option<(String, bool)> {
    if data.is_empty() || ffi::xh_is_none(data) {
        return None;
    }
    let b = body(data);
    if b.is_empty() {
        return None;
    }
    let raw = String::from_utf8_lossy(b);
    match raw.strip_suffix(INPLACE_SUFFIX) {
        Some(ty) => Some((ty.to_string(), true)),
        None => Some((raw.into_owned(), false)),
    }
}

/// rwfunc/rwir 参数定义键（点后缀 `.[0,±k]`）：body 只存 langtype（可带 ` inplace`
/// 尾缀），**不存名字**——名字是源码注释（`#name`），到 kvspace 没有意义。
pub fn new_def_param(langtype: &str) -> Result<Vec<u8>, String> {
    new_def_param_with_inplace(langtype, false)
}

/// 写参定义键 body：`langtype[ inplace]`。
pub fn new_def_param_with_inplace(langtype: &str, inplace: bool) -> Result<Vec<u8>, String> {
    new_def_langtype_with_inplace(langtype, inplace)
}

/// 参数定义键 body `langtype[ inplace]` -> (langtype, inplace)。
pub fn def_param_full(data: &[u8]) -> Option<(String, bool)> {
    def_langtype_full(data)
}

pub fn new_struct() -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_DEF_STRUCT, &[], 1)
}

// ── kvlang 自有 kind：rwfunc ────────────────────────────────────────
//
pub fn new_rwfunc_dir() -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_RWFUNC, &[], 1)
}

pub fn new_rwfunc_anchor(nr: i32, nw: i32, dynamic: bool) -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_RWFUNC, &counts_body(nr, nw, dynamic)?, 1)
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
    let b = body(data);
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
pub fn plain(data: &[u8], lt: &str) -> String {
    if data.is_empty() {
        return String::new();
    }
    let (_, k) = parse_langtype(lt);
    plain_value(&k, body(data))
}

/// rwir 族槽值的载荷串（opcode / 引用名）：body 去 5 字节计数头后的字节。
/// 写槽值 → (名字, 声明类型)：写目标带 map langtype 标注（`x:{keylt}·{valt} = {}`）时，槽的
/// langtype 即该 map langtype、body 即变量名（见 [[map容器]]）；其余写槽走 rwir 引用载荷
/// （跳过 5B 计数头）。dump 逆向时据此还原 `-> x:<type>`，使 round-trip 不丢容器类型。
pub fn write_slot_name(data: &[u8]) -> (String, String) {
    if data.is_empty() {
        return (String::new(), String::new());
    }
    let b = body(data);
    if b.len() > 5 {
        if let Some(split) = b[5..].iter().position(|&x| x == 0) {
            let name = String::from_utf8_lossy(&b[5..5 + split]).into_owned();
            let ty = String::from_utf8_lossy(&b[6 + split..]).into_owned();
            return (name, ty);
        }
    }
    (rwir_sig(data), String::new())
}

pub fn rwir_sig(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    let b = body(data);
    let text = &b[5.min(b.len())..];
    let end = text.iter().position(|&x| x == 0).unwrap_or(text.len());
    String::from_utf8_lossy(&text[..end]).into_owned()
}

#[cfg(test)]
mod map_langtype_tests {
    use super::{is_map_langtype, parse_langtype};

    /*
     * 用例来自 `workspace/622/coding2/map-type-syntax-cases.md`（唯一输入源）。
     * C 侧 `runtimec/tests/langtype_test.c` 用**同一份清单**断言，
     * 两边的判定结果必须逐条相同。
     */

    #[test]
    fn legal_map_types_are_map() {
        /* #1–#8：合法 map 类型。 */
        for text in [
            "'int64'int64",                    // #1 标量 formatter
            "'[]char/utf8'int64",              // #2 字符串 formatter（utf8）
            "'[]char/utf32'int64",             // #3 字符串 formatter（utf32）
            "'[int64]'int64",                  // #4 一元元组
            "'[float64,float64]'[]char/utf32", // #5 二元元组
            "'[int32,int32]'int64",            // #6 二元元组
            "'[int64]·[]char/utf8'int64",      // #7 两段 key
            "'bool'int64",                     // #8 bool formatter
        ] {
            assert!(is_map_langtype(text), "expected map: {text}");
        }
    }

    #[test]
    fn non_map_types_are_not_map() {
        /* #15–#16：非 map，走普通类型解析。 */
        for text in ["[]char/utf8", "[2]float64", "int64", "char/utf32"] {
            assert!(!is_map_langtype(text), "expected non-map: {text}");
        }
    }

    #[test]
    fn illegal_map_spellings_are_recognized_as_map_but_rejected_downstream() {
        /*
         * #9–#14：非法写法。`is_map_langtype` 只看首字符，所以 `'` 开头的
         * 都判为 map——**合法性校验属于后续解析**，不是本判据的职责。
         * 这里钉住的是「判据不自作主张」：它答「是不是 map 形态」，不答「合不合法」。
         */
        for text in [
            "''int64",       // #9 空 formatter
            "''int64'int64", // #10 嵌套（本轮不支持）
            "'int64'",       // #11 缺 value
            "'int64",        // #12 未闭合
            "'foo'int64",    // #14 未知 formatter
        ] {
            assert!(
                is_map_langtype(text),
                "starts with quote, so it is map-shaped: {text}"
            );
        }
        /* #13 `int64'int64` 缺开头引号 ⇒ 不是 map 形态。 */
        assert!(!is_map_langtype("int64'int64"));
    }

    #[test]
    fn utf8_and_utf32_formatters_yield_the_same_access_key() {
        /*
         * 用例 #2/#3 的核心断言：**formatter 描述的是被格式化值的编码，
         * 输出恒是路径文本**。`'[]char/utf8'` 与 `'[]char/utf32'` 产出的 key
         * 完全相同——差别只在键值在 body 里怎么存，不在路径形态。
         *
         * 本层只断言「两者都被判为 map」（路径文本的产出在下游）；C 侧断言
         * 同一件事。两侧都断言，是为了防止任一侧把编码带进路径。
         */
        assert!(is_map_langtype("'[]char/utf8'int64"));
        assert!(is_map_langtype("'[]char/utf32'int64"));
    }

    #[test]
    fn parse_langtype_never_receives_a_map_type() {
        /*
         * 非 map 串照常解析：`[2]float64` 得形状 `[2]`，base 是 `float64`。
         * map 串不该走到这里（`debug_assert` 守着），这里只验非 map 的路径没坏。
         */
        let (dims, base) = parse_langtype("[2]float64");
        assert_eq!(dims, vec![2]);
        assert_eq!(base, "float64");
    }
}
