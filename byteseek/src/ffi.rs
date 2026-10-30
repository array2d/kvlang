//! C ABI bindings for kvspace, kvlang runtime, and layout.

use std::ffi::{c_char, c_int, c_void, CStr, CString};

/// 读 head 结果（与 layout/src/ffi.rs 的 kvspaceHead_t 逐字段对齐）。
/// 三正交轴 ref×storetype×langtype；langtype 由 kvspaceGetHead 从值 attr 区读入。
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

// ── 原位 head 访问（v2 固定 8 字节 head：#566，偏移直取，不建结构、不校验）──

/// wire: [len:u48 LE@0-5][storetype:u8@6][ptr:u8@7][body@8]；body 偏移恒 = 8。
pub const XH_PREFIX: usize = 8;

#[inline]
pub fn xh_len(data: &[u8]) -> u64 {
    let b = data.get(0..6).unwrap_or(&[]);
    let mut v = 0u64;
    for i in (0..b.len()).rev() {
        v = (v << 8) | u64::from(b[i]);
    }
    v
}
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
#[inline]
pub fn xh_is_ext(data: &[u8]) -> bool {
    xh_ptr(data) == 2
}
/// None：空字节，或固定头 len=0 且非 ptr/ext（SHORT 空值）。
#[inline]
pub fn xh_is_none(data: &[u8]) -> bool {
    data.is_empty() || (data.len() >= XH_PREFIX && xh_len(data) == 0 && xh_ptr(data) == 0)
}

/// body 切片（偏移恒 = XH_PREFIX，长度 = len）；越界 → 空。
pub fn xh_body_slice(data: &[u8]) -> &[u8] {
    let len = xh_len(data) as usize;
    let Some(end) = XH_PREFIX.checked_add(len) else {
        return &[];
    };
    if end > data.len() {
        return &[];
    }
    &data[XH_PREFIX..end]
}

/// 从值 wire 解析 attr 区取 tag=1（langtype）记录 data；无/坏 attr → 空串。
/// attr = [attr_len:u16 LE@8+len][records...]，record = [tag:u8][len:u16 LE][data]。
pub fn xh_langtype(data: &[u8]) -> String {
    let Some(apos) = XH_PREFIX.checked_add(xh_len(data) as usize) else {
        return String::new();
    };
    let Some(a) = data.get(apos..apos + 2) else {
        return String::new();
    };
    let alen = u16::from_le_bytes([a[0], a[1]]) as usize;
    if alen < 2 {
        return String::new();
    }
    let Some(aend) = apos.checked_add(alen) else {
        return String::new();
    };
    if aend > data.len() {
        return String::new();
    }
    let mut p = apos + 2;
    while p + 3 <= aend {
        let tag = data[p];
        let rlen = u16::from_le_bytes([data[p + 1], data[p + 2]]) as usize;
        if p + 3 + rlen > aend {
            return String::new(); // 坏记录：越界即整体无效
        }
        if tag == 1 {
            return String::from_utf8_lossy(&data[p + 3..p + 3 + rlen]).into_owned();
        }
        p += 3 + rlen;
    }
    String::new()
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

/// 把内联 XValue 组装成 wire（固定 8 字节 head + body + 空 attr 占位）。
/// storetype 判定对齐 durable to_wire_head：char/* 与 rwir 族→SLACK、ndim>0 非容器→LARGE、
/// 其余标量→SHORT。langtype 记录（含 [dims]）由写层按真实类型替换（#571 值内 attr 区）。
pub fn xvalue_to_wire(kind: &str, raw: &[u8], dims: &[i32]) -> Vec<u8> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    if (kind.is_empty() || kind == "None") && raw.is_empty() {
        return call_codec64(|out, out_len| unsafe { kvspaceXvalueHeadNewNone(out, out_len) });
    }
    if kind == "char" || kind.starts_with("char/") {
        return call_codec64(|out, out_len| unsafe {
            kvspaceXvalueHeadNewSlack(raw.as_ptr(), raw.len() as u64, raw.len() as u64, empty, out, out_len)
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
            kvspaceXvalueHeadNewCode(raw.as_ptr(), raw.len() as u64, raw.len() as u64, empty, out, out_len)
        });
    }
    call_codec64(|out, out_len| unsafe {
        kvspaceXvalueHeadNewScalar(0, raw.as_ptr(), raw.len() as u32, empty, out, out_len)
    })
}

/// 完整 langtype 串（#571）：ndim0 = 裸 kind；ndim>0 = [d1,d2,...]kind。
pub fn full_langtype(kind: &str, dims: &[i32]) -> String {
    if dims.is_empty() {
        return kind.to_string();
    }
    let d: Vec<String> = dims.iter().map(|x| x.to_string()).collect();
    format!("[{}]{kind}", d.join(","))
}

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

/// 用 langtype 记录替换 codec 产出的空 attr 占位：[head+body] + attr(lt)。
/// wire 已自带 langtype 记录 → 原样写（不覆盖）。
pub fn with_langtype(wire: &[u8], lt: &str) -> Vec<u8> {
    if !xh_langtype(wire).is_empty() {
        return wire.to_vec();
    }
    let mut w = wire[..XH_PREFIX + xh_len(wire) as usize].to_vec();
    w.extend_from_slice(&attr_bytes(lt));
    w
}

/// 读 key 的完整 langtype（kvspaceGetHead 从值 attr 区读入）；无/空 → 空串。
pub fn langtype_of(h: *mut c_void, key: &str) -> String {
    let mut out = kvspaceHead_t {
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
    let rc = unsafe { kvspaceGetHead(h, cs(key).as_ptr(), &mut out) };
    if rc != 0 {
        return String::new();
    }
    let n = out
        .langtype
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(out.langtype.len());
    String::from_utf8_lossy(&out.langtype[..n]).into_owned()
}

#[cfg(not(feature = "embed"))]
unsafe extern "C" {
    // kvspace ABI.
    pub fn kvspaceConnect(dsn: *const c_char) -> *mut c_void;
    pub fn kvspaceClear(h: *mut c_void, err: *mut c_char, err_cap: u32) -> c_int;
    pub fn kvspaceDelTree(
        h: *mut c_void,
        prefix: *const c_char,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn kvspaceDel(
        h: *mut c_void,
        keys: *const *const c_char,
        nkeys: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    /// 借用读：*out 指向后端常驻/回收空间，调用方不得 free。resolve=1 穿透 link。
    pub fn kvspaceGet(
        h: *mut c_void,
        key: *const c_char,
        resolve: c_int,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    pub fn kvspaceSetValue(
        h: *mut c_void,
        key: *const c_char,
        value: *const u8,
        value_len: u32,
        ro: u8,
        vid: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    // 前缀遍历：listlen 定计数，逐 idx 取名（借用回收缓冲，不得 free），不一次性返回整段名单。
    pub fn kvspaceListLen(
        h: *mut c_void,
        prefix: *const c_char,
        expand_ext: c_int,
        resolve: c_int,
        out_count: *mut i32,
    ) -> c_int;
    pub fn kvspaceListAt(
        h: *mut c_void,
        prefix: *const c_char,
        expand_ext: c_int,
        resolve: c_int,
        idx: i32,
        buf: *mut u8,
        buf_cap: u32,
        out_len: *mut u32,
    ) -> c_int;
    // #571：固定 8 字节 head 构造器（无转换层，直接 memcpy struct + body + attr 区；
    // langtype 空 = 空 attr 占位）。out_len 为 u64。
    pub fn kvspaceXvalueHeadNewNone(out: *mut *mut u8, out_len: *mut u64) -> c_int;
    pub fn kvspaceXvalueHeadNewScalar(
        storetype: u8,
        raw: *const u8,
        raw_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvspaceXvalueHeadNewSlack(
        data: *const u8,
        data_len: u64,
        cap: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvspaceXvalueHeadNewTensor(
        dims: *const u64,
        ndim: u32,
        raw: *const u8,
        raw_len: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvspaceXvalueHeadNewCode(
        body: *const u8,
        body_len: u64,
        cap: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn kvspaceXvalueHeadNewExt(
        locator: *const u8,
        locator_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    /// 读 head + 值 attr 区 langtype 记录：缺失/空 → 非 0。langtype 为明文事实源。
    pub fn kvspaceGetHead(
        h: *mut c_void,
        key: *const c_char,
        out: *mut kvspaceHead_t,
    ) -> c_int;

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
