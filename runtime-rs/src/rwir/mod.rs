//! rwir —— 注册进 kvlang runtime 的纯净 stdlib（无副作用外世界依赖，全部 in-process）：
//!   term    : print / println / cerr / input     行输出 + 读 stdin
//!   json    : json·to / json·from                 KV 子树 ↔ JSON 文本
//!   http    : http·call                            网络抓取（ureq 原生）
//!   kvlayout: kvlanglayout·vet/layout/src          自造 kv 代码入库（layout C ABI）
//! 不纯 rwir（llm/shell/python/byteseek·run 等）留在 byteseek，依赖本库后自行叠加。

pub mod http;
pub mod json;
pub mod kvlayout;
pub mod term;

use crate::engine::Engine;
use crate::ffi::*;
use std::collections::HashSet;
use std::sync::OnceLock;

/// rwir 注册表：(opcode, 读参数, 写参数, 签名)。
pub const REGS: &[(&str, i32, i32, &str)] = &[
    ("input", 1, 1, "any\nany"),
    ("print", 1, 0, "any..."),
    ("println", 1, 0, "any..."),
    ("cerr", 1, 0, "any..."),
    ("json·to", 1, 1, "any\nany"),
    ("json·from", 1, 1, "any\nany"),
    (
        "http·call",
        4,
        1,
        "[]char/utf32\n[]char/utf32\n[]char/utf32\n[]char/utf32\n[]char/utf32",
    ),
    ("kvlanglayout·vet", 1, 1, "[]char/utf32\n[]char/utf32"),
    ("kvlanglayout·format", 1, 1, "[]char/utf32\n[]char/utf32"),
    ("kvlanglayout·layout", 1, 1, "[]char/utf32\n[]char/utf32"),
    ("kvlanglayout·dump", 1, 1, "[]char/utf32\n[]char/utf32"),
];

pub fn register(eng: &Engine) {
    for (op, nr, nw, sig) in REGS {
        unsafe { kvlang_rwirextRegister(eng.kv, cs(op).as_ptr(), *nr, *nw, cs(sig).as_ptr()) };
    }
}

/// 本库能就地处理的 rwir（驱动循环据此批处理，不移交外部进程）。
pub fn is_inproc(op: &str) -> bool {
    matches!(
        op,
        "print"
            | "println"
            | "cerr"
            | "input"
            | "json·to"
            | "json·from"
            | "http·call"
            | "kvlanglayout·vet"
            | "kvlanglayout·format"
            | "kvlanglayout·layout"
            | "kvlanglayout·dump"
    )
}

/// 进程内已注册的 rwir opcode 集合（不读 /lib，直接本地过滤）。
static REGISTERED: OnceLock<HashSet<&'static str>> = OnceLock::new();

fn registered() -> &'static HashSet<&'static str> {
    REGISTERED.get_or_init(|| REGS.iter().map(|r| r.0).collect())
}

/// 判「别人的 rwir」：opcode 是否已注册（进程内 map）。
pub fn is_others_rwir(op: &str) -> bool {
    registered().contains(op)
}

/// 主导驱动循环遇到就地 rwir 时分派。
pub fn dispatch(eng: &Engine, op: &str, pc: &str) {
    match op {
        "print" | "println" | "cerr" => term::print_line(eng, pc),
        "input" => term::input(eng, pc),
        "json·to" => json::to(eng, pc),
        "json·from" => json::from(eng, pc),
        "http·call" => http::call(eng, pc),
        "kvlanglayout·vet" => {
            let out = kvlayout::vet(eng, &eng.read0(pc));
            eng.set_kv(&eng.write0(pc), &out);
        }
        "kvlanglayout·format" => {
            let out = kvlayout::format(eng, &eng.read0(pc));
            eng.set_kv(&eng.write0(pc), &out);
        }
        "kvlanglayout·layout" => {
            let out = kvlayout::layout(eng, &eng.read0(pc));
            eng.set_kv(&eng.write0(pc), &out);
        }
        "kvlanglayout·dump" => {
            let out = kvlayout::dump(eng, &eng.read0(pc));
            eng.set_kv(&eng.write0(pc), &out);
        }
        other => eprintln!("kvlang: 未知 rwir: {other} @ {pc}"),
    }
}
