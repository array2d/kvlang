//! rwir —— 注册进 kvlang runtime 的 stdlib，文件/模块按 kvlang lib 嵌套关系组织
//! （lib 用 `/` 嵌套、成员用 `·`，文件树与之 1:1）：
//!   term         : print / println / cerr / printf / input   行输出 + C 风格格式化 + 读 stdin（裸 rwir）
//!   json         : json·to / json·from                   KV 子树 ↔ JSON 文本
//!   http         : http·call                             网络抓取（ureq 原生）
//!   kvlanglayout : kvlang·vet/format/layout/printlib/printstack  自造 kv 代码入库与自省（layout C ABI）
//!   networld     : networld/proc·exec、networld/fs·size/read/write/append/rename/list/del/mkdir/exists/isutf8   外部进程 + 宿主文件系统
//! 不纯 rwir（llm/shell/python/byteseek·run 等）留在 byteseek，依赖本库后自行叠加。

pub mod http;
pub mod json;
pub mod kvlanglayout;
pub mod math;
pub mod networld;
pub mod term;

use crate::engine::{Engine, KvResult};
use crate::ffi::*;
use std::collections::HashMap;
use std::ffi::CString;
use std::os::raw::c_char;
use std::sync::OnceLock;

/// In-process rwir handler; returning `Err` marks the owning vthread as `error`.
pub type Handler = fn(&Engine, &str, &str) -> KvResult;

/// Single rwir entry: per-slot read/write langtype lists and in-process handler.
/// 读参、写参默认 `*T`（一层指针，见 #329 评论「关于五.7 指针」）——参数 key 是 `[s,i]`，
/// i 正为写参、负为读参，`*T` 约定覆盖两者。union 逐 branch 加 `*`，变参尾缀 `...` 保持在最后。
/// 例外：xvalue 初始化类 rwir 的参数是真实值 `T`（`input`/`json·from`，见
/// runtime/src/kvcpu.c 的 rwir_init_byval）。
pub struct Rwir {
    pub rp: &'static [&'static str],
    pub wp: &'static [&'static str],
    pub run: Handler,
}

const fn rw(rp: &'static [&'static str], wp: &'static [&'static str], run: Handler) -> Rwir {
    Rwir { rp, wp, run }
}

// Common slot type patterns.
const STR: &str = "*[]char/utf8|*[]char/utf32"; // UTF-8 or UTF-32 string
const UTF32: &str = "*[]char/utf32";
const TEXT: &[&str] = &[UTF32];
const NUM1: &[&str] = &["*float64|*int64"];
const NUM2: &[&str] = &["*float64|*int64", "*float64|*int64"];
const F64: &[&str] = &["*float64"];
const BOOL: &[&str] = &["*bool"];
const I64: &[&str] = &["*int64"];

/// Single source of truth for in-process rwir ops (used for registration, membership, and dispatch).
pub const MYRWIRCAPS: &[(&str, Rwir)] = &[
    // input：xvalue 初始化类（读 stdin 初值写入写槽）。读参豁免 *T → 真实值（prompt）；
    // 写参是「写到哪」= 指针靶，仍守 *T 约定。
    ("input", rw(&[""], &["*"], |e, _, pc| term::input(e, pc))),
    ("print", rw(&["*..."], &[], |e, _, pc| term::print_line(e, pc))),
    ("println", rw(&["*..."], &[], |e, _, pc| term::print_line(e, pc))),
    ("cerr", rw(&["*..."], &[], |e, _, pc| term::print_line(e, pc))),
    ("printf", rw(&[STR, "*..."], &[], |e, _, pc| term::printf(e, pc))),
    ("json·to", rw(&["*"], &["*"], |e, _, pc| json::to(e, pc))),
    // json·from：xvalue 初始化类（以源 JSON 文本为初值构造 xvalue 树）。读参豁免 *T →
    // 真实值（源文本）；写参是「写到哪」= 指针靶，仍守 *T 约定。
    ("json·from", rw(&[""], &["*"], |e, _, pc| json::from(e, pc))),
    (
        "http·call",
        rw(&[UTF32, UTF32, UTF32, UTF32], TEXT, |e, _, pc| http::call(e, pc)),
    ),
    ("kvlang·vet", rw(TEXT, TEXT, |e, _, pc| text_rwir(e, pc, kvlanglayout::vet))),
    ("kvlang·format", rw(TEXT, TEXT, |e, _, pc| text_rwir(e, pc, kvlanglayout::format))),
    ("kvlang·layout", rw(TEXT, TEXT, |e, _, pc| text_rwir(e, pc, kvlanglayout::layout))),
    ("kvlang·printlib", rw(TEXT, TEXT, |e, _, pc| text_rwir(e, pc, kvlanglayout::printlib))),
    (
        "kvlang·printstack",
        rw(TEXT, TEXT, |e, _, pc| {
            let out = kvlanglayout::printstack(e, pc, &e.read0(pc));
            e.set_kv(&e.write0(pc), &out)
        }),
    ),
    (
        "networld/proc·exec",
        rw(
            &["*'[int64]'[]char/utf32", "*'[int64]'[]char/utf32"],
            &["*uint8", "*[]uint8", "*[]uint8"],
            |e, _, pc| networld::proc::exec(e, pc),
        ),
    ),
    ("networld/fs·size", rw(&[STR], I64, |e, _, pc| networld::fs::size(e, pc))),
    (
        "networld/fs·read",
        rw(&[STR, "*int64", "*int64"], &["*[]uint8"], |e, _, pc| networld::fs::read(e, pc)),
    ),
    ("networld/fs·write", rw(&[STR, "*[]uint8"], I64, |e, _, pc| networld::fs::write(e, pc))),
    ("networld/fs·rename", rw(&[STR, STR], I64, |e, _, pc| networld::fs::rename(e, pc))),
    ("networld/fs·isutf8", rw(&[STR], BOOL, |e, _, pc| networld::fs::isutf8(e, pc))),
    ("networld/fs·append", rw(&[STR, "*[]uint8"], I64, |e, _, pc| networld::fs::append(e, pc))),
    (
        "networld/fs·list",
        rw(&[STR], &["*'[int64]'[]char/utf32"], |e, _, pc| networld::fs::list(e, pc)),
    ),
    ("networld/fs·del", rw(&[STR], I64, |e, _, pc| networld::fs::del(e, pc))),
    ("networld/fs·mkdir", rw(&[STR], I64, |e, _, pc| networld::fs::mkdir(e, pc))),
    ("networld/fs·exists", rw(&[STR], BOOL, |e, _, pc| networld::fs::exists(e, pc))),
    ("math·sin", rw(NUM1, F64, math::dispatch)),
    ("math·cos", rw(NUM1, F64, math::dispatch)),
    ("math·tan", rw(NUM1, F64, math::dispatch)),
    ("math·asin", rw(NUM1, F64, math::dispatch)),
    ("math·acos", rw(NUM1, F64, math::dispatch)),
    ("math·atan", rw(NUM1, F64, math::dispatch)),
    ("math·atan2", rw(NUM2, F64, math::dispatch)),
    ("math·sinh", rw(NUM1, F64, math::dispatch)),
    ("math·cosh", rw(NUM1, F64, math::dispatch)),
    ("math·tanh", rw(NUM1, F64, math::dispatch)),
    ("math·floor", rw(NUM1, F64, math::dispatch)),
    ("math·ceil", rw(NUM1, F64, math::dispatch)),
    ("math·round", rw(NUM1, F64, math::dispatch)),
    ("math·trunc", rw(NUM1, F64, math::dispatch)),
    ("math·log2", rw(NUM1, F64, math::dispatch)),
    ("math·log10", rw(NUM1, F64, math::dispatch)),
    ("math·hypot", rw(NUM2, F64, math::dispatch)),
    ("math·isnan", rw(NUM1, BOOL, math::dispatch)),
    ("math·isinf", rw(NUM1, BOOL, math::dispatch)),
    ("math·isfinite", rw(NUM1, BOOL, math::dispatch)),
];

/// Helper for text-to-text `kvlang·*` ops (read arg 0 -> write arg 0).
fn text_rwir(eng: &Engine, pc: &str, f: fn(&Engine, &str) -> String) -> KvResult {
    let out = f(eng, &eng.read0(pc));
    eng.set_kv(&eng.write0(pc), &out)
}

/// Lookup map from opcode to `&Rwir`.
static RWIRMAP: OnceLock<HashMap<&'static str, &'static Rwir>> = OnceLock::new();
pub fn rwirmap() -> &'static HashMap<&'static str, &'static Rwir> {
    RWIRMAP.get_or_init(|| MYRWIRCAPS.iter().map(|(op, r)| (*op, r)).collect())
}

pub fn register(eng: &Engine) {
    for (op, r) in MYRWIRCAPS {
        let rp: Vec<CString> = r.rp.iter().map(|s| cs(s)).collect();
        let wp: Vec<CString> = r.wp.iter().map(|s| cs(s)).collect();
        let rpp: Vec<*const c_char> = rp.iter().map(|c| c.as_ptr()).collect();
        let wpp: Vec<*const c_char> = wp.iter().map(|c| c.as_ptr()).collect();
        unsafe {
            kvlangDefRwir(
                eng.kv,
                cs(op).as_ptr(),
                rpp.as_ptr(),
                rpp.len() as i32,
                wpp.as_ptr(),
                wpp.len() as i32,
            )
        };
    }
}

/// Reports whether `op` is handled in-process (`MYRWIRCAPS`).
pub fn is_inproc(op: &str) -> bool {
    rwirmap().contains_key(op)
}

/// Alias of [`is_inproc`] for `myrwircaps` checks.
pub fn in_myrwircaps(op: &str) -> bool {
    is_inproc(op)
}

/// Dispatch an in-process rwir op; `Err` marks the owning vthread as `error`.
pub fn dispatch(eng: &Engine, op: &str, pc: &str) -> KvResult {
    match rwirmap().get(op) {
        Some(r) => (r.run)(eng, op, pc),
        None => Err(format!("unknown rwir: {op} @ {pc}")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Verify the table-driven op set matches the 44 built-in ops.
    #[test]
    fn table_ops_match_previous_set() {
        const PREVIOUS: &[&str] = &[
            "input", "print", "println", "cerr", "printf", "json·to", "json·from", "http·call",
            "kvlang·vet", "kvlang·format", "kvlang·layout", "kvlang·printlib", "kvlang·printstack",
            "networld/proc·exec", "networld/fs·size", "networld/fs·read", "networld/fs·write",
            "networld/fs·rename", "networld/fs·isutf8", "networld/fs·append", "networld/fs·list",
            "networld/fs·del", "networld/fs·mkdir", "networld/fs·exists", "math·sin", "math·cos",
            "math·tan", "math·asin", "math·acos", "math·atan", "math·atan2", "math·sinh",
            "math·cosh", "math·tanh", "math·floor", "math·ceil", "math·round", "math·trunc",
            "math·log2", "math·log10", "math·hypot", "math·isnan", "math·isinf", "math·isfinite",
        ];
        let ops: Vec<&str> = MYRWIRCAPS.iter().map(|(op, _)| *op).collect();
        assert_eq!(ops, PREVIOUS, "registration order / op set changed");
        assert_eq!(rwirmap().len(), MYRWIRCAPS.len(), "duplicate opcode in MYRWIRCAPS");
        for op in PREVIOUS {
            assert!(is_inproc(op) && in_myrwircaps(op), "{op}");
        }
        for op in ["", "vthread·run", "numpy·add", "math·exp"] {
            assert!(!is_inproc(op) && !in_myrwircaps(op), "{op}");
        }
        // Unknown ops must return Err rather than being silently ignored.
        let eng = Engine {
            rt: std::ptr::null_mut(),
            kv: std::ptr::null_mut(),
            dsn: String::new(),
            ext: None,
        };
        assert!(dispatch(&eng, "numpy·add", "/vthread/1/[0]/[0,0]").is_err());
    }

    #[test]
    fn embedded_stdlib_all_vet() {
        let dsn = format!("shm:///tmp/kvlang-stdlib-vet-{}", std::process::id());
        let kv = unsafe { kvspaceConnect(cs(&dsn).as_ptr()) };
        assert!(!kv.is_null());
        let eng = Engine {
            rt: std::ptr::null_mut(),
            kv,
            dsn,
            ext: None,
        };
        for (name, src) in crate::stdlib::EMBEDDED_KV {
            assert_eq!(
                kvlanglayout::vet(&eng, src),
                "ok",
                "stdlib/{name} failed vet"
            );
        }
    }
}
