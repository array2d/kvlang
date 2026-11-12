
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

pub type Handler = fn(&Engine, &str, &str) -> KvResult;

pub struct Rwir {
    pub rp: &'static [&'static str],
    pub wp: &'static [&'static str],
    pub run: Handler,
}

const fn rw(rp: &'static [&'static str], wp: &'static [&'static str], run: Handler) -> Rwir {
    Rwir { rp, wp, run }
}

const STR: &str = "*[]char/utf8|*[]char/utf32"; 
const UTF32: &str = "*[]char/utf32";
const TEXT: &[&str] = &[UTF32];
const NUM1: &[&str] = &["*float64|*int64"];
const NUM2: &[&str] = &["*float64|*int64", "*float64|*int64"];
const F64: &[&str] = &["*float64"];
const BOOL: &[&str] = &["*bool"];
const I64: &[&str] = &["*int64"];

pub const MYRWIRCAPS: &[(&str, Rwir)] = &[
    
    ("input", rw(&[""], &["*"], |e, _, pc| term::input(e, pc))),
    ("print", rw(&["*..."], &[], |e, _, pc| term::print_line(e, pc))),
    ("println", rw(&["*..."], &[], |e, _, pc| term::print_line(e, pc))),
    ("cerr", rw(&["*..."], &[], |e, _, pc| term::print_line(e, pc))),
    ("printf", rw(&[STR, "*..."], &[], |e, _, pc| term::printf(e, pc))),
    ("json·to", rw(&["*"], &["*"], |e, _, pc| json::to(e, pc))),
    
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

fn text_rwir(eng: &Engine, pc: &str, f: fn(&Engine, &str) -> String) -> KvResult {
    let out = f(eng, &eng.read0(pc));
    eng.set_kv(&eng.write0(pc), &out)
}

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

pub fn is_inproc(op: &str) -> bool {
    rwirmap().contains_key(op)
}

pub fn in_myrwircaps(op: &str) -> bool {
    is_inproc(op)
}

pub fn dispatch(eng: &Engine, op: &str, pc: &str) -> KvResult {
    match rwirmap().get(op) {
        Some(r) => (r.run)(eng, op, pc),
        None => Err(format!("unknown rwir: {op} @ {pc}")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
