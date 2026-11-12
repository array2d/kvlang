
pub const MEMBER_SEP: &str = "·"; 
pub const RUNTIME_MEMBER_SEP: &str = "\u{2025}"; 
pub const SEG_LABELS: &str = "labels"; 

pub const SEG_INFERRED: &str = "inferred";

pub const SEG_CRASH_RESUME: &str = "crash_resume";
pub const SRC_EXT: &str = ".src"; 

pub const LIB_ROOT: &str = "/lib";
pub const RWIR_ROOT: &str = "/lib";

pub fn lib_func(pkg: &str, name: &str) -> String {
    if pkg.is_empty() {
        format!("{LIB_ROOT}/{name}")
    } else {
        format!("{LIB_ROOT}/{pkg}{MEMBER_SEP}{name}")
    }
}

pub fn lib_src(pkg: &str, name: &str) -> String {
    if pkg.is_empty() {
        format!("{LIB_ROOT}/{name}{SRC_EXT}")
    } else {
        format!("{LIB_ROOT}/{pkg}{MEMBER_SEP}{name}{SRC_EXT}")
    }
}

pub fn lib_labels_dir(pkg: &str, name: &str) -> String {
    format!("{}/{RUNTIME_MEMBER_SEP}{SEG_LABELS}/", lib_func(pkg, name))
}

pub fn lib_label(pkg: &str, name: &str, label: &str) -> String {
    format!("{}{label}", lib_labels_dir(pkg, name))
}

pub fn rwir(opcode: &str) -> String {
    format!("{RWIR_ROOT}/{opcode}")
}

pub fn member(base: &str, name: &str) -> String {
    format!("{base}{MEMBER_SEP}{name}")
}
