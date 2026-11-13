
#![allow(non_snake_case, non_camel_case_types)]

#[cfg(feature = "embed")]
pub mod embed;
pub mod engine;
pub mod ffi;
pub mod rwir;

pub fn exe() -> &'static str {
    use std::sync::OnceLock;
    static E: OnceLock<String> = OnceLock::new();
    E.get_or_init(|| {
        std::env::current_exe()
            .ok()
            .and_then(|p| p.file_name().map(|s| s.to_string_lossy().into_owned()))
            .unwrap_or_else(|| "pathseek".into())
    })
}

#[macro_export]
macro_rules! elog {
    ($($a:tt)*) => {{ eprintln!("{}: {}", $crate::exe(), format_args!($($a)*)); }};
}

pub mod stdlib {
    include!(concat!(env!("OUT_DIR"), "/embedded_kv.rs"));
}
