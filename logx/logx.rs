//! Diagnostic logging — matching logx/logx.go.
use std::env;

pub fn debug(args: std::fmt::Arguments) { if level() <= 0 { eprintln!("{}", args); } }
pub fn info(args: std::fmt::Arguments)  { if level() <= 1 { eprintln!("{}", args); } }
pub fn warn(args: std::fmt::Arguments)  { if level() <= 2 { eprintln!("warn: {}", args); } }
pub fn error(args: std::fmt::Arguments) { if level() <= 3 { eprintln!("error: {}", args); } }
pub fn fatal(args: std::fmt::Arguments) { error(args); std::process::exit(1); }

fn level() -> i32 {
    match env::var("LOG_LEVEL").unwrap_or_default().as_str() {
        "debug" => 0, "info" => 1, "warn" | "" => 2, "error" => 3, _ => 2,
    }
}

#[macro_export] macro_rules! log_debug { ($($arg:tt)*) => { $crate::logx::logx::debug(format_args!($($arg)*)); }; }
#[macro_export] macro_rules! log_info  { ($($arg:tt)*) => { $crate::logx::logx::info(format_args!($($arg)*)); }; }
#[macro_export] macro_rules! log_warn  { ($($arg:tt)*) => { $crate::logx::logx::warn(format_args!($($arg)*)); }; }
#[macro_export] macro_rules! log_error { ($($arg:tt)*) => { $crate::logx::logx::error(format_args!($($arg)*)); }; }
