//! Logging — identical to logx/logx.go and logx/logx.h.
pub fn debug(msg: &str) { eprintln!("[DEBUG] {msg}") }
pub fn info(msg: &str) { eprintln!("[INFO] {msg}") }
pub fn warn(msg: &str) { eprintln!("[WARN] {msg}") }
pub fn error(msg: &str) { eprintln!("[ERROR] {msg}") }
pub fn fatal(msg: &str) -> ! { eprintln!("[FATAL] {msg}"); std::process::exit(1) }
