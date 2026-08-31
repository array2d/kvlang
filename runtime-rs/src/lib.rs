//! kvlang-rs —— 功能完整的 kvlang runtime 库。
//!   ffi     : C ABI 绑定（kvspace / kvlang runtime / kvlang layout）
//!   engine  : Engine，kvspace 读写 / TLV / 读写槽解析等原语
//!   rwir    : 纯净 rwir stdlib（term/json/http/kvlanglayout）
//! 二进制 `kvlang`（src/main.rs）在此之上加 CLI + 模式2 驱动循环；
//! byteseek 等外部 crate 可直接依赖本库复用 Engine/ffi/纯 rwir，再叠加自有 rwir。
#![allow(non_snake_case, non_camel_case_types)]

pub mod engine;
pub mod ffi;
pub mod rwir;

/// 内嵌 stdlib kv 源码（lib/**/*.kv，由 build.rs 生成）：(相对名, 源码)。
/// 启动时 layout 进 kvspace，使 http·get 等 rwfunc 可解析。外部 crate 亦可复用。
pub mod stdlib {
    include!(concat!(env!("OUT_DIR"), "/embedded_kv.rs"));
}
