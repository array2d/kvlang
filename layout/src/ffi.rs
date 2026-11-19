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

    // 新存储 ABI：读走 Resolve + 进程内基址，写走 Realloc + 直写，枚举走 Listlen + List。
    // 旧 ABI 的 Get/SetValue/Del/ListLen/ListAt/DelTree 已从库里删除，不再声明。

    /// key → 偏移。0 是合法偏移，错误由返回码表达（不用 0/-1 当哨兵）。
    fn kvspaceResolveOffset(
        h: Handle,
        key: *const c_char,
        parent_key_anchor: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 删除 key 的值并释放 box；空索引连索引节点一起删。
    fn kvspaceFree(h: Handle, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    /// 进程内值区基址。地址 = 基址 + offset；非映射后端（durable）返回 NULL。
    fn kvspaceXvalueInProcessAddress(h: Handle) -> *mut u8;
    /// 直接子项数 N 与最长子项名字节数 M；非索引返回 NOT_INDEX，空索引 N=M=0。
    fn kvspaceListlen(
        h: Handle,
        key: *const c_char,
        out_count: *mut i64,
        out_max_name_length: *mut u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 一次取全部直接子项名（NUL 分隔，后端顺序、不排序）。
    fn kvspaceList(
        h: Handle,
        key: *const c_char,
        out_names: *mut c_char,
        out_capacity: u32,
        out_length: *mut u32,
        out_count: *mut u32,
        out_generation: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceDeltree(h: Handle, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceMkindex(h: Handle, path: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    /// **新 ABI（store-bound）**：构造器直接把 wire 写进 kvspace 的映射，返回指向
    /// 该映射的指针（借用，生命周期到该 key 下一次 Realloc/Free）；失败返回 NULL。
    /// 形参里 `handle` 是 kvspace 句柄、`key` 是目标键——**类型与键都在构造期定死**，
    /// 不再是「先造一串字节、事后再 set」。
    ///
    /// 唯一事实源：`kvlangxvalue/kvlangxvalue.h`。旧的 `kvlangXvalueNewScalar`
    /// 已不存在（改名 `NewT`）；其余七个的旧形态是
    /// `(…, langtype, out, out_len) -> int`，新形态是
    /// `(handle, key, …, langtype) -> uint8_t*`。**个数与语义都变了**——动态链接期
    /// 不报错，跑到才 UB，所以这里必须与头逐字对齐。
    fn kvlangXvalueNewT(
        handle: *mut c_void,
        key: *const c_char,
        raw: *const u8,
        raw_len: u32,
        langtype: *const c_char,
    ) -> *mut u8;
    fn kvlangXvalueNewBufferArray(
        handle: *mut c_void,
        key: *const c_char,
        data: *const u8,
        data_len: u64,
        langtype: *const c_char,
    ) -> *mut u8;
    fn kvlangXvalueNewCompactArray(
        handle: *mut c_void,
        key: *const c_char,
        dims: *const u64,
        ndim: u32,
        raw: *const u8,
        raw_len: u64,
        langtype: *const c_char,
    ) -> *mut u8;
    fn kvlangXvalueNewCode(
        handle: *mut c_void,
        key: *const c_char,
        body: *const u8,
        body_len: u64,
        langtype: *const c_char,
    ) -> *mut u8;
    fn kvlangXvalueNewChar(
        handle: *mut c_void,
        key: *const c_char,
        bytes: *const u8,
        len: u32,
        langtype: *const c_char,
    ) -> *mut u8;
    fn kvlangXvalueNewBool(
        handle: *mut c_void,
        key: *const c_char,
        v: u8,
        langtype: *const c_char,
    ) -> *mut u8;
    fn kvlangXvalueNewInt64(
        handle: *mut c_void,
        key: *const c_char,
        v: i64,
        langtype: *const c_char,
    ) -> *mut u8;
    fn kvlangXvalueNewFloat64(
        handle: *mut c_void,
        key: *const c_char,
        v: f64,
        langtype: *const c_char,
    ) -> *mut u8;
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

/// store-bound 构造器的统一出口：调一次 C 构造器，拿回**映射内的 wire 指针**，
/// 成功返回 `()`，失败返回错误串。
///
/// **不再有 `Vec<u8>`**：新 ABI 里构造器直接把 wire 写进 kvspace 的映射，返回值
/// 是借用指针（生命周期到该 key 下一次 Realloc/Free）。旧模型的「造一串字节再
/// `Kv::set`」在这个模型下不存在——`set` 已删。
///
/// `what` 只用于错误信息，指到是哪个构造器失败。
fn call_ctor(
    what: &str,
    key: &str,
    f: impl FnOnce(*mut c_void, *const c_char) -> *mut u8,
) -> Result<(), String> {
    let ck = CString::new(key).expect("no NUL in key");
    let wire = f(std::ptr::null_mut(), ck.as_ptr());
    if wire.is_null() {
        return Err(format!("kvlangxvalue {what} failed for {key:?}"));
    }
    Ok(())
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

/// 测试用 DSN：`shm://<临时路径>`。
///
/// **为什么不是 `fs://`**：单库化后 `libkvspace` 只服务 `shm://`（`kvspace_dsn_path`
/// 拒绝其它 scheme）。而 `shm` 本身就是 file-backed mmap——持久化、重开、崩溃恢复
/// 这些能力它都有。layout 的测试要验的是**布局语义**，不是后端语义，换一个真能跑
/// 的后端比跳过它更接近本意。
///
/// `tag` 用来区分同一测试进程内的多个 store，避免互相覆盖。
pub fn test_dsn(tag: &str) -> String {
    format!("shm:///tmp/kvlanglayout_{}_{}", tag, std::process::id())
}

/// 删除一个 shm store 的全部文件。
///
/// shm 后端一个 store 是**四个文件**（`<path>`、`.garb`、`.gen`、`.sbo.data`、
/// `.sbo.head`），不是目录——`remove_dir_all` 对它无效。全删干净才不会在
/// /tmp 累积（表观几百 MB、实际 sparse 几 KB，但文件数会堆）。
pub fn remove_test_store(dsn: &str) {
    let path = dsn.trim_start_matches("shm://");
    for suffix in ["", ".garb", ".gen", ".sbo.data", ".sbo.head"] {
        let _ = std::fs::remove_file(format!("{path}{suffix}"));
    }
}

/// 尝试连接；失败返回 None（不 panic）。
///
/// 给「后端本期不支持」这类**环境缺口**用：单库化后 `libkvspace` 只服务
/// `shm://`，`fs://` 会在 DSN 解析处被拒。那是后端能力边界，不是被测代码错，
/// 按「跳过≠损坏」应当显式 SKIP 并打印原因，而不是长期红着让人以为是回归。
pub fn try_connect(dsn: &str) -> Option<Handle> {
    let c = CString::new(dsn).ok()?;
    let h = unsafe { kvspaceConnect(c.as_ptr()) };
    (!h.is_null()).then_some(h)
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

    /// kvspace 句柄，供 store-bound 构造器使用。
    ///
    /// 构造器签名是 `(void *handle, const char *key, …)`——句柄是**参数**，不是全局、
    /// 不是字段缓存、不是 thread_local。谁要构造值就把 `&mut Kv` 传到哪儿。
    pub fn handle(&self) -> Handle {
        self.h
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

    /// 删一个 key（`None` 语义：值不存在）。
    ///
    /// **没有 `set` 了**：新 ABI 的构造器直接把 wire 写进映射，构造即落库，不存在
    /// 「攒一批 (key, lt, Vec<u8>) 再一次写」的批写模型。留一个 `set` 的壳去承接
    /// 旧的三元组，就是让旧模型借尸还魂（p5）。
    pub fn del(&mut self, key: &str) -> Result<(), String> {
        let ck = CString::new(key).expect("no NUL in key");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe { kvspaceFree(self.h, ck.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        if ret == 0 || ret == -2 {
            return Ok(());
        }
        err_ret(&mut err, ret)
    }

    /// 单点读：resolve 拿 offset，再从进程内基址拷出整条 wire（自持）。
    /// 不存在 / 后端非映射 → 空字节（调用方按「没有这个值」处理）。
    /// 就地改 body 里的一个 u16（小端）。`offset` 是 body 内偏移。
    ///
    /// 这是**测试专用**的「模拟畸形值」入口：新 ABI 下没有 `Kv::set` 可以塞任意字节，
    /// 而有些测试要验「槽被外部改成不一致」时的行为。走 resolve 拿映射地址直接改，
    /// 不重写 head——改的就是 wire 本体。
    pub fn patch_body_u16(&mut self, key: &str, offset: usize, v: u16) -> Result<(), String> {
        let c = CString::new(key).expect("no NUL in key");
        let mut off: u64 = 0;
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            kvspaceResolveOffset(self.h, c.as_ptr(), 0, &mut off, err.as_mut_ptr(), err.len() as u32)
        };
        if ret != 0 {
            return err_ret(&mut err, ret);
        }
        let base = unsafe { kvspaceXvalueInProcessAddress(self.h) };
        if base.is_null() {
            return Err(format!("{key}: backend has no in-process address space"));
        }
        unsafe {
            let dst = base.add(off as usize + 16 + offset) as *mut u8;
            std::ptr::copy_nonoverlapping(v.to_le_bytes().as_ptr(), dst, 2);
        }
        Ok(())
    }

    pub fn get_one(&mut self, key: &str) -> Vec<u8> {
        let c = CString::new(key).expect("no NUL in key");
        let mut off: u64 = 0;
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            kvspaceResolveOffset(
                self.h,
                c.as_ptr(),
                0,
                &mut off,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        if ret != 0 {
            return Vec::new();
        }
        let base = unsafe { kvspaceXvalueInProcessAddress(self.h) };
        if base.is_null() {
            return Vec::new();
        }
        unsafe {
            // 整条 wire 的长度由 head 自己算出：16 + bodylen + attrslen。
            let head = std::slice::from_raw_parts(base.add(off as usize), 16);
            let body_len = u64::from_le_bytes(head[0..8].try_into().unwrap()) as usize;
            let attrs_len = u16::from_le_bytes([head[8], head[9]]) as usize;
            let total = 16 + body_len + attrs_len;
            std::slice::from_raw_parts(base.add(off as usize), total).to_vec()
        }
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

    /// 直接子项名（后端顺序、**不排序**）。`expand_ext` / `resolve` 两个旧参数
    /// 已无对应语义：新 ABI 的 List 只列物理直接子项，不展开 extindex、不解析 Ptr。
    /// 参数保留是为了不动调用点签名，实现忽略它们。
    pub fn list(&mut self, prefix: &str, _expand_ext: bool, _resolve: bool) -> Vec<String> {
        let c = CString::new(prefix).expect("no NUL in prefix");
        let mut count: i64 = 0;
        let mut max_name: u32 = 0;
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            kvspaceListlen(
                self.h,
                c.as_ptr(),
                &mut count,
                &mut max_name,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        if ret != 0 || count <= 0 {
            return Vec::new();
        }
        // listlen 给出的 N 和 M 就是为了这一次分配：N*(M+1) 必然装得下全部名字。
        let cap = (count as usize).saturating_mul(max_name as usize + 1);
        if cap == 0 {
            return Vec::new();
        }
        let mut buf = vec![0u8; cap];
        let mut length: u32 = 0;
        let mut listed: u32 = 0;
        let mut generation: u64 = 0;
        let ret = unsafe {
            kvspaceList(
                self.h,
                c.as_ptr(),
                buf.as_mut_ptr() as *mut c_char,
                cap as u32,
                &mut length,
                &mut listed,
                &mut generation,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        if ret != 0 {
            return Vec::new();
        }
        buf[..length as usize]
            .split(|&b| b == 0)
            .filter(|s| !s.is_empty())
            .map(|s| String::from_utf8_lossy(s).into_owned())
            .collect()
    }

    pub fn del_tree(&mut self, prefix: &str) -> Result<(), String> {
        let c = CString::new(prefix).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe { kvspaceDeltree(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }

    pub fn mkindex(&mut self, path: &str) -> Result<(), String> {
        let c = CString::new(path).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe { kvspaceMkindex(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
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

/// **kind → 构造器的唯一映射点**（in-place，构造即落库）。
///
/// 入参从「kind + raw → Vec<u8>」变成「kv + key + kind + raw → ()」：新 ABI 的构造函数
/// 直接把 wire 写进 kvspace 的映射，没有中间字节串。**保留这个分派点**是为了不让
/// `kind` 的 switch 复制到每个调用方——它是单一路径。
///
/// 形态选择（与旧版一致，只是改成就地写）：
///   - 空 kind / `None` 且空 body → BufferArray（16 字节空头，零字节会被存储层拒绝）；
///   - `char/*` → BufferArray（变长字符）；
///   - 带 dims 且非 map 非路径 → CompactArray（定长张量）；
///   - `rwir` / `def langtype` / 非空 `rwfunc` → NewCode（指令体）；
///   - 其余标量 → NewT。
pub fn xvalue_to_wire(
    kv: &mut Kv,
    key: &str,
    kind: &str,
    raw: &[u8],
    array_len: i32,
    langtype: &str,
) -> Result<(), String> {
    let dims = al_to_dims(kind, array_len);
    let h = kv.handle();
    let ck = CString::new(key).expect("no NUL in key");
    let lt = langtype_cstr(langtype);
    if (kind.is_empty() || kind == "None") && raw.is_empty() {
        return call_ctor("NewBufferArray", key, |_, _| unsafe {
            kvlangXvalueNewBufferArray(h, ck.as_ptr(), std::ptr::null(), 0, lt.as_ptr())
        });
    }
    if kind == "char" || kind.starts_with("char/") {
        return call_ctor("NewBufferArray", key, |_, _| unsafe {
            kvlangXvalueNewBufferArray(
                h, ck.as_ptr(), raw.as_ptr(), raw.len() as u64, lt.as_ptr(),
            )
        });
    }
    if !dims.is_empty() && !kind.contains('\u{b7}') && !kind.starts_with('/') {
        let d: Vec<u64> = dims.iter().map(|&x| x as u64).collect();
        return call_ctor("NewCompactArray", key, |_, _| unsafe {
            kvlangXvalueNewCompactArray(
                h, ck.as_ptr(), d.as_ptr(), d.len() as u32,
                raw.as_ptr(), raw.len() as u64, lt.as_ptr(),
            )
        });
    }
    if kind == "rwir" || kind == "def langtype" || (kind == "rwfunc" && !raw.is_empty()) {
        return call_ctor("NewCode", key, |_, _| unsafe {
            kvlangXvalueNewCode(
                h, ck.as_ptr(), raw.as_ptr(), raw.len() as u64, lt.as_ptr(),
            )
        });
    }
    call_ctor("NewT", key, |_, _| unsafe {
        kvlangXvalueNewT(h, ck.as_ptr(), raw.as_ptr(), raw.len() as u32, lt.as_ptr())
    })
}

/// 从一条 wire 里取 langtype 文本（attr tag 1）；没有该记录 → None。
///
/// attr 区自描述：`[tag:u8][len:u16][data]`，紧跟 body 之后，长度由 head 的
/// `attrslen` 给出（**没有**独立长度前缀）。几何全部取自 head，不写偏移算术。
pub fn wire_langtype(wire: &[u8]) -> Option<String> {
    if wire.len() < PREFIX {
        return None;
    }
    if !xh_padding(wire).is_some_and(|p| p.iter().all(|&b| b == 0)) {
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

// ── 便利构造器（同样 in-place；`langtype` 在构造期给定，不许事后补 attr）──────

/// `char/<enc>` 字符串。`char/utf8` 走原始字节；`char/utf32` 每字符 4 字节小端。
pub fn new_char(kv: &mut Kv, key: &str, kind: &str, s: &str, langtype: &str) -> Result<(), String> {
    let bytes = s.as_bytes();
    if kind == "char/utf8" {
        return new_char_byte(kv, key, bytes, langtype);
    }
    let raw: Vec<u8> = if kind == "char/utf32" {
        s.chars().flat_map(|c| (c as u32).to_le_bytes()).collect()
    } else {
        bytes.to_vec()
    };
    let h = kv.handle();
    let ck = CString::new(key).expect("no NUL in key");
    let lt = langtype_cstr(langtype);
    call_ctor("NewBufferArray", key, |_, _| unsafe {
        kvlangXvalueNewBufferArray(h, ck.as_ptr(), raw.as_ptr(), raw.len() as u64, lt.as_ptr())
    })
}

pub fn new_char_byte(kv: &mut Kv, key: &str, bytes: &[u8], langtype: &str) -> Result<(), String> {
    let h = kv.handle();
    let ck = CString::new(key).expect("no NUL in key");
    let lt = langtype_cstr(langtype);
    call_ctor("NewChar", key, |_, _| unsafe {
        kvlangXvalueNewChar(h, ck.as_ptr(), bytes.as_ptr(), bytes.len() as u32, lt.as_ptr())
    })
}

/// 空值字面量 `None`：16 字节空头（空 body、无 langtype）。
/// **不是零字节**——存储层以 `invalid XValue` 拒绝零字节值。
pub fn new_none(kv: &mut Kv, key: &str) -> Result<(), String> {
    let h = kv.handle();
    let ck = CString::new(key).expect("no NUL in key");
    let lt = langtype_cstr("");
    call_ctor("NewBufferArray", key, |_, _| unsafe {
        kvlangXvalueNewBufferArray(h, ck.as_ptr(), std::ptr::null(), 0, lt.as_ptr())
    })
}

pub fn new_bool(kv: &mut Kv, key: &str, v: bool, langtype: &str) -> Result<(), String> {
    let h = kv.handle();
    let ck = CString::new(key).expect("no NUL in key");
    let lt = langtype_cstr(langtype);
    call_ctor("NewBool", key, |_, _| unsafe {
        kvlangXvalueNewBool(h, ck.as_ptr(), v as u8, lt.as_ptr())
    })
}

pub fn new_int64(kv: &mut Kv, key: &str, v: i64, langtype: &str) -> Result<(), String> {
    let h = kv.handle();
    let ck = CString::new(key).expect("no NUL in key");
    let lt = langtype_cstr(langtype);
    call_ctor("NewInt64", key, |_, _| unsafe {
        kvlangXvalueNewInt64(h, ck.as_ptr(), v, lt.as_ptr())
    })
}

pub fn new_float64(kv: &mut Kv, key: &str, v: f64, langtype: &str) -> Result<(), String> {
    let h = kv.handle();
    let ck = CString::new(key).expect("no NUL in key");
    let lt = langtype_cstr(langtype);
    call_ctor("NewFloat64", key, |_, _| unsafe {
        kvlangXvalueNewFloat64(h, ck.as_ptr(), v, lt.as_ptr())
    })
}
