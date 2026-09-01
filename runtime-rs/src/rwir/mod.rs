//! rwir —— 注册进 kvlang runtime 的纯净 stdlib（无副作用外世界依赖，全部 in-process）：
//!   term    : print / println / cerr / input     行输出 + 读 stdin
//!   json    : json·to / json·from                 KV 子树 ↔ JSON 文本
//!   http    : http·call                            网络抓取（ureq 原生）
//!   kvlayout: kvlanglayout·vet/layout/src          自造 kv 代码入库（layout C ABI）
//! 不纯 rwir（llm/shell/python/byteseek·run 等）留在 byteseek，依赖本库后自行叠加。

pub mod http;
pub mod internet;
pub mod json;
pub mod kvlayout;
pub mod term;

use crate::engine::Engine;
use crate::ffi::*;
use std::collections::HashMap;
use std::sync::OnceLock;

/// 单个 rwir 的签名：读参 / 写参各自独立的 kindexpr 列表（逐槽一型，不假设同型）。
pub struct Rwir {
    pub rp: &'static [&'static str],
    pub wp: &'static [&'static str],
}

/// rwir 注册表：key = 去 `/lib` 后的 opcode，value = 每槽 kindexpr（读参 rp / 写参 wp）。
pub const REGS: &[(&str, Rwir)] = &[
    (
        "input",
        Rwir {
            rp: &["any"],
            wp: &["any"],
        },
    ),
    (
        "print",
        Rwir {
            rp: &["any..."],
            wp: &[],
        },
    ),
    (
        "println",
        Rwir {
            rp: &["any..."],
            wp: &[],
        },
    ),
    (
        "cerr",
        Rwir {
            rp: &["any..."],
            wp: &[],
        },
    ),
    (
        "json·to",
        Rwir {
            rp: &["any"],
            wp: &["any"],
        },
    ),
    (
        "json·from",
        Rwir {
            rp: &["any"],
            wp: &["any"],
        },
    ),
    (
        "http·call",
        Rwir {
            rp: &[
                "[]char/utf32",
                "[]char/utf32",
                "[]char/utf32",
                "[]char/utf32",
            ],
            wp: &["[]char/utf32"],
        },
    ),
    (
        "kvlanglayout·vet",
        Rwir {
            rp: &["[]char/utf32"],
            wp: &["[]char/utf32"],
        },
    ),
    (
        "kvlanglayout·format",
        Rwir {
            rp: &["[]char/utf32"],
            wp: &["[]char/utf32"],
        },
    ),
    (
        "kvlanglayout·layout",
        Rwir {
            rp: &["[]char/utf32"],
            wp: &["[]char/utf32"],
        },
    ),
    (
        "kvlanglayout·dump",
        Rwir {
            rp: &["[]char/utf32"],
            wp: &["[]char/utf32"],
        },
    ),
    (
        "internet/proc·exec",
        Rwir {
            rp: &["[]stringkeymap", "[]stringkeymap"],
            wp: &["uint8"],
        },
    ),
];

/// opcode → &Rwir，供注册与逐槽 kindexpr 查询。
static RWIRMAP: OnceLock<HashMap<&'static str, &'static Rwir>> = OnceLock::new();
pub fn rwirmap() -> &'static HashMap<&'static str, &'static Rwir> {
    RWIRMAP.get_or_init(|| REGS.iter().map(|(op, r)| (*op, r)).collect())
}

pub fn register(eng: &Engine) {
    for (op, r) in REGS {
        let sig =
            r.rp.iter()
                .chain(r.wp.iter())
                .copied()
                .collect::<Vec<_>>()
                .join("\n");
        unsafe {
            kvlang_rwirextRegister(
                eng.kv,
                cs(op).as_ptr(),
                r.rp.len() as i32,
                r.wp.len() as i32,
                cs(&sig).as_ptr(),
            )
        };
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
            | "internet/proc·exec"
    )
}

/// 判「别人的 rwir」：opcode 是否已注册（进程内 map）。
pub fn is_others_rwir(op: &str) -> bool {
    rwirmap().contains_key(op)
}

/// 主导驱动循环遇到就地 rwir 时分派。
pub fn dispatch(eng: &Engine, op: &str, pc: &str) {
    match op {
        "print" | "println" | "cerr" => term::print_line(eng, pc),
        "input" => term::input(eng, pc),
        "json·to" => json::to(eng, pc),
        "json·from" => json::from(eng, pc),
        "http·call" => http::call(eng, pc),
        "internet/proc·exec" => internet::exec(eng, pc),
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
