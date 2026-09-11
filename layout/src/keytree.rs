//! KV 路径与成员分隔符统一管理。所有构造 KV 路径的地方均须使用本模块常量。

// ── 常量 ─────────────────────────────────────────────────────────────

pub const MEMBER_SEP: &str = "·"; // 成员访问分隔符（U+00B7 中点号）：释放 '.' 供小数 key 使用
pub const RUNTIME_MEMBER_SEP: &str = "\u{2025}"; // U+2025，运行时保留字段前缀
pub const SEG_LABELS: &str = "labels"; // /lib/<func>/‥labels/<name> → irseq
pub const SRC_EXT: &str = ".src"; // 函数源码文件后缀

pub const LIB_ROOT: &str = "/lib";
pub const RWIR_ROOT: &str = "/lib";

// ── /lib ─────────────────────────────────────────────────────────────

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

/// `/lib/<pkg>·<name>/‥labels/`
pub fn lib_labels_dir(pkg: &str, name: &str) -> String {
    format!("{}/{RUNTIME_MEMBER_SEP}{SEG_LABELS}/", lib_func(pkg, name))
}

/// `/lib/<pkg>·<name>/‥labels/<label>`
pub fn lib_label(pkg: &str, name: &str, label: &str) -> String {
    format!("{}{label}", lib_labels_dir(pkg, name))
}

pub fn rwir(opcode: &str) -> String {
    format!("{RWIR_ROOT}/{opcode}")
}

// ── 成员 ─────────────────────────────────────────────────────────────

pub fn member(base: &str, name: &str) -> String {
    format!("{base}{MEMBER_SEP}{name}")
}
