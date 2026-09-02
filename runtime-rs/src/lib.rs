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

/// 当前可执行文件名（basename），缓存。runtime 诊断日志前缀用它，
/// 以区别于 kvcode 的 print/println（走 stdout、无前缀）。
pub fn exe() -> &'static str {
    use std::sync::OnceLock;
    static E: OnceLock<String> = OnceLock::new();
    E.get_or_init(|| {
        std::env::current_exe()
            .ok()
            .and_then(|p| p.file_name().map(|s| s.to_string_lossy().into_owned()))
            .unwrap_or_else(|| "kvlang".into())
    })
}

/// runtime 诊断日志：`<exe>: <msg>` 到 stderr。与 kvcode print（stdout）区分。
#[macro_export]
macro_rules! elog {
    ($($a:tt)*) => {{ eprintln!("{}: {}", $crate::exe(), format_args!($($a)*)); }};
}

/// 内嵌 stdlib kv 源码（lib/**/*.kv，由 build.rs 生成）：(相对名, 源码)。
/// 启动时 layout 进 kvspace，使 http·get 等 rwfunc 可解析。外部 crate 亦可复用。
pub mod stdlib {
    include!(concat!(env!("OUT_DIR"), "/embedded_kv.rs"));
}
