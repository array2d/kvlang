//! C ABI bindings for kvspace, kvlang runtime, and layout.
//!
//! **线格式（XValue head / attr 区）只有公共头一份**：`kvspace/include/kvspace/
//! xvalue_head.h`。本文件不再自带任何解析或拼装 —— 曾从 `layout/src/ffi.rs` 复制
//! 过一份 8 字节 head 的手写实现（`XH_PREFIX` / `xh_*` / `attr_bytes` /
//! `with_langtype`），上游改成 16 字节后那份没跟上，编译绿而读值是错的。
//! 禁止再复制：需要读/造值就调公共头的 `kvspaceXvalueGetAttr` /
//! `kvspaceXvalueHeadEffectiveLen` 与 `kvlangXvalueHeadNew*`。
//! （`kvspaceXvalueHeadScan` 已随新 ABI 删除，attr 区读入口只剩 GetAttr。）

use std::ffi::{c_char, c_int, c_void, CStr, CString};

// ── 值读取：全部委托公共头（`kvspaceXvalueGetAttr` / `EffectiveLen`）──────
//
// 这里**不算任何偏移**：body 起点的唯一算术在公共头里（`KVSPACE_XVALUE_HEAD_LEN`
// + 有效长度）。本层只做「切片 + 转 String」。

/// `KVSPACE_XVALUE_HEAD_LEN`：head 定长 16。**唯一允许出现在本文件里的偏移常量**。
pub const HEAD_LEN: usize = 16;

/// `KVLANG_XVALUE_ATTR_TAG_LANGTYPE`：attr 区里 langtype 记录的 tag。
pub const KVSPACE_ATTR_TAG_LANGTYPE: u8 = 1;

/// 值的完整 langtype 串（含 `[dims]` 与 `*`/`@` 前缀）；无/坏 → 空串。
pub fn value_langtype(wire: &[u8]) -> String {
    let mut lt: *const u8 = std::ptr::null();
    let mut len: u16 = 0;
    // `kvspaceXvalueHeadScan` 已删除：改用 attr 区唯一读入口 GetAttr(tag=1)。
    let rc = unsafe {
        kvspaceXvalueGetAttr(wire.as_ptr(), wire.len() as u64,
                             KVSPACE_ATTR_TAG_LANGTYPE, &mut lt, &mut len)
    };
    if rc != 0 || lt.is_null() || len == 0 {
        return String::new();
    }
    String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(lt, len as usize) }).into_owned()
}

/// 值的**有效** body 长度。容量形态（bufferarray）下 head 的 `bodylen` 是容量，
/// 必须走这条取 tag 2 的 reallen。
pub fn value_effective_len(wire: &[u8]) -> Option<usize> {
    let mut out: u64 = 0;
    let rc = unsafe { kvspaceXvalueHeadEffectiveLen(wire.as_ptr(), wire.len() as u64, &mut out) };
    if rc != 0 || out > usize::MAX as u64 {
        return None;
    }
    Some(out as usize)
}

/// body 切片。长度取有效长度（容量形态不拖零尾巴）；越界 → 空。
pub fn value_body(wire: &[u8]) -> &[u8] {
    let Some(len) = value_effective_len(wire) else {
        return &[];
    };
    let Some(end) = HEAD_LEN.checked_add(len) else {
        return &[];
    };
    if end > wire.len() {
        return &[];
    }
    &wire[HEAD_LEN..end]
}

/// 引用形态：只看 langtype 首字节（新模型：head 里没有 ptr/storetype）。
pub fn value_is_ptr(wire: &[u8]) -> bool {
    value_langtype(wire).as_bytes().first() == Some(&b'*')
}

pub fn value_is_ext(wire: &[u8]) -> bool {
    value_langtype(wire).as_bytes().first() == Some(&b'@')
}

/// 固定头构造器调用（#566：New* 的 out_len 为 u64）：成功且 total<=u32::MAX 才转 u32。
fn call_codec64(f: impl FnOnce(*mut *mut u8, *mut u64) -> c_int) -> Vec<u8> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u64 = 0;
    f(&mut out, &mut out_len);
    if out.is_null() || out_len == 0 || out_len > u32::MAX as u64 {
        if !out.is_null() {
            unsafe { libc::free(out as *mut c_void) };
        }
        return Vec::new();
    }
    let bytes = unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec();
    unsafe { libc::free(out as *mut c_void) };
    bytes
}

/// 空 C 串（codec langtype 参数：空 = 空 attr 占位，真实类型由写层 with_langtype 填入）。
pub static EMPTY_CSTR: [u8; 1] = [0];

/// 完整 langtype 串：ndim0 = 裸 kind；ndim>0 = `[d1,d2,...]kind`。
/// 这是**类型串构造**，不是线格式——构造器直接吃它。
pub fn full_langtype(kind: &str, dims: &[i32]) -> String {
    if dims.is_empty() {
        return kind.to_string();
    }
    let d: Vec<String> = dims.iter().map(|x| x.to_string()).collect();
    format!("[{}]{kind}", d.join(","))
}

/// 所有构造点的**唯一出口**：调公共构造器，再用公共头回读自检。
///
/// 自检两条（不相等即 panic——这是程序错误，不是数据错误）：
///   1. `Scan` 回的 langtype 与传入的逐字节相同；
///   2. `EffectiveLen` 等于有效长度。
///
/// 存在的理由：构造器实参错位（本项目真实发生过一次 `storetype` 形参多一个、
/// 整体错一格）不会崩、不会报错，只在下游读成垃圾。一次回读就在构造点当场抓住。
fn build_checked(f: impl FnOnce(*mut *mut u8, *mut u64) -> c_int,
                 want_langtype: &str, want_len: usize) -> Vec<u8> {
    let bytes = call_codec64(f);
    if bytes.is_empty() {
        return bytes;
    }
    let got_lt = value_langtype(&bytes);
    assert_eq!(got_lt, want_langtype,
               "constructor langtype mismatch: want {want_langtype:?}, wire has {got_lt:?}");
    let got_len = value_effective_len(&bytes)
        .unwrap_or_else(|| panic!("constructed wire has no effective length"));
    assert_eq!(got_len, want_len,
               "constructor effective length mismatch: want {want_len}, wire has {got_len}");
    bytes
}

/// 把内联 XValue 组装成 wire。**形态是值的语义，不是构造器的偏好**：
/// 定长走 `NewScalar`、变长字符走 `NewSlack`、张量走 `NewTensor`、
/// 代码槽走 `NewCode`——把 `char/*` 从 Slack 改成 Scalar 会改变 wire 的
/// layout/容量语义。
///
/// 指针/扩展**不走这里**：`NewPtr`/`NewExt` 是唯一给 langtype 补 `*`/`@` 的构造点，
/// 调用方要造指针值请直接调 `ptr_wire` / `ext_wire`。
pub fn xvalue_to_wire(kind: &str, raw: &[u8], dims: &[i32]) -> Vec<u8> {
    let lt = full_langtype(kind, dims);
    let c_lt = CString::new(lt.clone()).expect("langtype contains NUL");
    if !dims.is_empty() && !kind.contains('\u{b7}') && !kind.starts_with('/') {
        let d: Vec<u64> = dims.iter().map(|&x| x as u64).collect();
        return build_checked(
            |out, out_len| unsafe {
                kvlangXvalueHeadNewTensor(d.as_ptr(), d.len() as u32,
                                           raw.as_ptr(), raw.len() as u64,
                                           c_lt.as_ptr(), out, out_len)
            },
            &lt, raw.len());
    }
    if kind == "char" || kind.starts_with("char/") {
        return build_checked(
            |out, out_len| unsafe {
                kvlangXvalueHeadNewSlack(raw.as_ptr(), raw.len() as u64,
                                          raw.len() as u64, c_lt.as_ptr(),
                                          out, out_len)
            },
            &lt, raw.len());
    }
    if kind == "rwir" || kind == "def langtype" || (kind == "rwfunc" && !raw.is_empty()) {
        return build_checked(
            |out, out_len| unsafe {
                kvlangXvalueHeadNewCode(raw.as_ptr(), raw.len() as u64,
                                         raw.len() as u64, c_lt.as_ptr(),
                                         out, out_len)
            },
            &lt, raw.len());
    }
    build_checked(
        |out, out_len| unsafe {
            kvlangXvalueHeadNewScalar(raw.as_ptr(), raw.len() as u32,
                                       c_lt.as_ptr(), out, out_len)
        },
        &lt, raw.len())
}

/// **无类型**值：空 langtype（公共头契约：空 langtype ⇒ 省略 langtype record）。
/// 只用于系统键（`‥pc`/`‥status`）——runtime 的 `write_text` 写的就是无类型值。
pub fn xvalue_to_wire_untyped(kind: &str, raw: &[u8], dims: &[i32]) -> Vec<u8> {
    let empty = CString::new("").expect("empty");
    if !dims.is_empty() && !kind.contains('\u{b7}') && !kind.starts_with('/') {
        let d: Vec<u64> = dims.iter().map(|&x| x as u64).collect();
        return build_checked(
            |out, out_len| unsafe {
                kvlangXvalueHeadNewTensor(d.as_ptr(), d.len() as u32,
                                           raw.as_ptr(), raw.len() as u64,
                                           empty.as_ptr(), out, out_len)
            },
            "", raw.len());
    }
    if kind == "char" || kind.starts_with("char/") {
        return build_checked(
            |out, out_len| unsafe {
                kvlangXvalueHeadNewSlack(raw.as_ptr(), raw.len() as u64,
                                          raw.len() as u64, empty.as_ptr(),
                                          out, out_len)
            },
            "", raw.len());
    }
    build_checked(
        |out, out_len| unsafe {
            kvlangXvalueHeadNewScalar(raw.as_ptr(), raw.len() as u32,
                                       empty.as_ptr(), out, out_len)
        },
        "", raw.len())
}

/// `None` 值（空 body、无 langtype）。
pub fn none_wire() -> Vec<u8> {
    build_checked(|out, out_len| unsafe { kvlangXvalueHeadNewNone(out, out_len) },
                  "", 0)
}

/// 指针值：`base_langtype` 是**目标**的 kindexpr（不带 `*`），构造器补前缀。
/// 传进来的若已带 `*`/`@`，公共构造器会拒绝（双重前缀 = 两处对格式理解不一致）。
pub fn ptr_wire(base_langtype: &str, path: &[u8]) -> Vec<u8> {
    let c_lt = CString::new(base_langtype).expect("langtype contains NUL");
    let want = format!("*{base_langtype}");
    build_checked(
        |out, out_len| unsafe {
            kvlangXvalueHeadNewPtr(path.as_ptr(), path.len() as u32,
                                    c_lt.as_ptr(), out, out_len)
        },
        &want, path.len())
}

/// 扩展句柄值：`base_langtype` 不带 `@`，构造器补前缀。
pub fn ext_wire(base_langtype: &str, locator: &[u8]) -> Vec<u8> {
    let c_lt = CString::new(base_langtype).expect("langtype contains NUL");
    let want = format!("@{base_langtype}");
    build_checked(
        |out, out_len| unsafe {
            kvlangXvalueHeadNewExt(locator.as_ptr(), locator.len() as u32,
                                    c_lt.as_ptr(), out, out_len)
        },
        &want, locator.len())
}

/// 读 key 的完整 langtype：Resolve 取偏移 → `InProcessAddress + off` 读 wire →
/// 公共头 `GetAttr(tag=1)` 解。
///
/// 偏移只在本次调用内有效（不跨调用持有）；wire 切片当场用完，不把借用带出去。
pub fn langtype_of(h: *mut c_void, key: &str) -> String {
    let mut off: u64 = 0;
    let mut err = [0u8; 256];
    let rc = unsafe {
        kvspaceResolveOffset(
            h, cs(key).as_ptr(), 0, &mut off,
            err.as_mut_ptr() as *mut c_char, err.len() as u32,
        )
    };
    if rc != 0 {
        return String::new();
    }
    let base = unsafe { kvspaceXvalueInProcessAddress(h) };
    if base.is_null() {
        return String::new();
    }
    unsafe {
        let wire_at = base.add(off as usize);
        let bodylen = u64::from_le_bytes(*(wire_at as *const [u8; 8]));
        let attrslen = u16::from_le_bytes(*(wire_at.add(8) as *const [u8; 2])) as u64;
        let Some(total) = (HEAD_LEN as u64).checked_add(bodylen).and_then(|n| n.checked_add(attrslen)) else {
            return String::new();
        };
        let wire = std::slice::from_raw_parts(wire_at, total as usize);
        value_langtype(wire)
    }
}

// ── runtimec ABI 的 Rust 侧镜像（TASK-042a）────────────────────────
//
// **逐字段对应 `runtimec/include/kvlang/runtimec.h`**，不这里发明字段。
// 结构漂移由 `create` 校验 `structure_size` 拦截：对不上直接拒绝，
// 不在 handler 里读错偏移。
//
// 头文件不安装到 `/usr/include`，所以只能手写镜像；唯一的防漂移手段就是
// `structure_size` —— 这也是**为什么 SDK 要用 `size_of::<T>()` 而不是写死数字**。

/// `runtimec_status_t`
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum RuntimecStatus {
    Success = 0,
    Yield = 1,
    Wait = 2,
    Unsupported = 3,
    NotImplemented = 4,
    Invalid = 5,
    OutOfMemory = 6,
    Error = 7,
    InsufficientCapacity = 8,
}
pub type runtimec_status_t = RuntimecStatus;

pub enum kvlang_runtime_t {}

/// `kvlang_runtime_text_buffer_t`
#[repr(C)]
#[derive(Clone, Copy)]
pub struct kvlang_runtime_text_buffer_t {
    pub data: *mut c_char,
    pub capacity: usize,
    pub length: usize,
    pub required_length: usize,
}

/// `kvlang_runtime_error_t`
#[repr(C)]
#[derive(Clone, Copy)]
pub struct kvlang_runtime_error_t {
    pub status: runtimec_status_t,
    pub operation_status: runtimec_status_t,
    pub code: i64,
    pub message: kvlang_runtime_text_buffer_t,
}

/// `kvlang_runtime_extension_handler_t`
pub type kvlang_runtime_extension_handler_t = Option<
    unsafe extern "C" fn(frame: *mut c_void, instruction: *const c_void, output_exit: *mut c_void) -> runtimec_status_t,
>;

/// `kvlang_runtime_extension_entry_t`：一条扩展算子。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct kvlang_runtime_extension_entry_t {
    pub structure_size: u32,
    pub operation_identifier: u32,
    pub flags: u32,
    pub handler: kvlang_runtime_extension_handler_t,
}

/// `kvlang_runtime_extension_module_t`：一组条目共用一个诊断名。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct kvlang_runtime_extension_module_t {
    pub module_name: *const c_char,
    pub entries: *const kvlang_runtime_extension_entry_t,
    pub entry_count: usize,
}

/// `kvlang_runtime_create_request_t`
#[repr(C)]
#[derive(Clone, Copy)]
pub struct kvlang_runtime_create_request_t {
    pub structure_size: usize,
    pub data_source_name: *const c_char,
    pub instance_identifier: u64,
    pub extensions: *const kvlang_runtime_extension_module_t,
    pub extension_count: usize,
}

/// `kvlang_runtime_entry_kind_t`
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum kvlang_runtime_entry_kind_t {
    Function = 0,
    ProgramCounter = 1,
    Vthread = 2,
    Bootstrap = 3,
}

/// `kvlang_runtime_execute_request_t`
#[repr(C)]
#[derive(Clone, Copy)]
pub struct kvlang_runtime_execute_request_t {
    pub entry_kind: kvlang_runtime_entry_kind_t,
    pub entry_name: *const c_char,
    pub arguments: *const *const c_char,
    pub argument_count: usize,
}

/// `kvlang_runtime_execute_result_t`
#[repr(C)]
#[derive(Clone, Copy)]
pub struct kvlang_runtime_execute_result_t {
    pub value: kvlang_runtime_text_buffer_t,
    pub program_counter: kvlang_runtime_text_buffer_t,
}

/// `kvlang_runtime_exit_kind_t`
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum kvlang_runtime_exit_kind_t {
    None = 0,
    Error = 1,
    Yield = 2,
}

/// 扩展条目的 `flags` 位（与 runtimec 内部 registry 同值）。
pub const RUNTIMEC_EXTENSION_FLAG_EFFECT: u32 = 1 << 0;
pub const RUNTIMEC_EXTENSION_FLAG_MAY_YIELD: u32 = 1 << 1;

#[cfg(not(feature = "embed"))]
unsafe extern "C" {
    // kvspace ABI.
    pub fn kvspaceConnect(dsn: *const c_char) -> *mut c_void;
    // ── 新 kvspace ABI（12 符号 + 构造器）：旧 SetValue/Get/ListLen/ListAt/
    //    DelTree/Del/Clear 已全部删除，不保留兼容层。 ────────────────────
    /// 进程内值区基址：地址 = base + value_offset。长连接句柄直接可用。
    pub fn kvspaceXvalueInProcessAddress(h: *mut c_void) -> *mut u8;
    /// key → 偏移。anchor 0 = root；不跟 Ptr。返回负 errno。
    pub fn kvspaceResolveOffset(
        h: *mut c_void,
        key: *const c_char,
        parent_key_anchor: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 唯一写原语：不存在则创建（清零），存在则调整容量。**head 由后端写好**，
    /// 调用方只填 body 与 attr。返回负 errno。
    pub fn kvspaceRealloc(
        h: *mut c_void,
        key: *const c_char,
        body_length: u64,
        attr_length: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 删值/空索引；非空索引返回 HAS_CHILDREN。
    pub fn kvspaceFree(h: *mut c_void, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    /// 递归删子树（连根删）；key 不存在视为成功。
    pub fn kvspaceDeltree(
        h: *mut c_void,
        key: *const c_char,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 直接子项数 N 与最长子项名字节数 M。
    pub fn kvspaceListlen(
        h: *mut c_void,
        key: *const c_char,
        out_count: *mut i64,
        out_max_name_length: *mut u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 一次性复制全部直接子项名（NUL 分隔，目录项带尾 `/`），后端顺序、不排序。
    /// out_names 为 NULL 时只回报长度/个数/generation。
    pub fn kvspaceList(
        h: *mut c_void,
        key: *const c_char,
        out_names: *mut c_char,
        out_capacity: u32,
        out_length: *mut u32,
        out_count: *mut u32,
        out_generation: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    // 固定 16 字节 head 构造器（`kvspace/include/kvspace/xvalue_head.h` 唯一实现）。
    // langtype 空 = 空 attr；out_len 为 u64。
    pub fn kvlangXvalueHeadNewNone(out: *mut *mut u8, out_len: *mut u64) -> c_int;
    /// 扫 attr 区取 tag=1（langtype）与 tag=2（reallen）。**返回 langtype 长度**，
    /// 0 = 缺失/坏值。`out` 被置为指向 wire 内部的借用指针（存活到 wire 失效）。
    /// attr 区唯一读入口：取 tag 的 data 起址与长度。返回 0 找到，-1 缺失/畸形。
    pub fn kvspaceXvalueGetAttr(
        wire: *const u8,
        wire_len: u64,
        tag: u8,
        out_data: *mut *const u8,
        out_len: *mut u16,
    ) -> c_int;
    /// 有效长度：有 attr tag 2 取 reallen，否则取 bodylen。
    pub fn kvspaceXvalueHeadEffectiveLen(
        wire: *const u8,
        wire_len: u64,
        out: *mut u64,
    ) -> c_int;
    pub fn kvlangXvalueHeadNewScalar(
        raw: *const u8,
        raw_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvlangXvalueHeadNewSlack(
        data: *const u8,
        data_len: u64,
        cap: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvlangXvalueHeadNewTensor(
        dims: *const u64,
        ndim: u32,
        raw: *const u8,
        raw_len: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvlangXvalueHeadNewCode(
        body: *const u8,
        body_len: u64,
        cap: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvlangXvalueHeadNewPtr(
        path: *const u8,
        path_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvlangXvalueHeadNewExt(
        locator: *const u8,
        locator_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;

    // ── runtimec 新 API（TASK-042a 骨架）──────────────────────────
    //
    // **与下面那批 kvlangRuntime*/kvlangRwirext* 并存**：新代码路径（骨架 + term）
    // 走这批；尚未迁移的 36 个 handler 仍走旧批（TASK-065 迁移完再删旧批）。
    // 这不是兼容层——是分阶段迁移的中间态，旧批的存在有明确到期条件。

    /// 建 runtime 实例。`request.structure_size` 必须等于本版本结构尺寸。
    pub fn kvlang_runtime_create(
        request: *const kvlang_runtime_create_request_t,
        output: *mut *mut kvlang_runtime_t,
        error: *mut kvlang_runtime_error_t,
    ) -> runtimec_status_t;
    pub fn kvlang_runtime_destroy(
        runtime: *mut kvlang_runtime_t,
        error: *mut kvlang_runtime_error_t,
    ) -> runtimec_status_t;
    pub fn kvlang_runtime_execute(
        runtime: *mut kvlang_runtime_t,
        request: *const kvlang_runtime_execute_request_t,
        output: *mut kvlang_runtime_execute_result_t,
        error: *mut kvlang_runtime_error_t,
    ) -> runtimec_status_t;

    /// handler 侧值访问：第 index 个读参的文本（char 族）。
    pub fn kvlang_runtime_read_argument_text(
        frame: *const c_void,
        instruction: *const c_void,
        index: usize,
        output: *mut kvlang_runtime_text_buffer_t,
        error: *mut kvlang_runtime_error_t,
    ) -> runtimec_status_t;
    pub fn kvlang_runtime_write_argument_wire(
        frame: *mut c_void,
        instruction: *const c_void,
        index: usize,
        data: *const u8,
        size: usize,
        error: *mut kvlang_runtime_error_t,
    ) -> runtimec_status_t;
    pub fn kvlang_runtime_set_exit(
        output_exit: *mut c_void,
        kind: kvlang_runtime_exit_kind_t,
        detail_code: i64,
        message: *const c_char,
        error: *mut kvlang_runtime_error_t,
    ) -> runtimec_status_t;

    // ── kvlang runtime：模式2 执行（kvlang_vthread.h）───────────────
    pub fn kvlangRuntimeConnect(dsn: *const c_char) -> *mut c_void;
    pub fn kvlangRuntimeDisconnect(rt: *mut c_void);
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

// #493 embed：符号在启动时 memfd+dlopen 后由 bind() 填指针；shim 与原声明同签名，
// 调用点零改动。
#[cfg(feature = "embed")]
include!(concat!(env!("OUT_DIR"), "/embed_shim.rs"));

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
