//! KV 路径与成员分隔符统一管理。所有构造 KV 路径的地方均须使用本模块常量。

// ── 常量 ─────────────────────────────────────────────────────────────

pub const MEMBER_SEP: &str = "·"; // 成员访问分隔符（U+00B7 中点号）：释放 '.' 供小数 key 使用
pub const RUNTIME_MEMBER_SEP: &str = "\u{2025}"; // U+2025，运行时保留字段前缀
pub const SEG_LABELS: &str = "labels"; // /lib/<func>/‥labels/<name> → irseq
/// `/lib/<func>/‥inferred/<name>` → 空：这个名字是**推断**局部（源里没有类型注解）。
///
/// 它承载的信息只有一件：**写这个槽时要不要按写槽声明定宽**。用户声明的槽是
/// 定宽的（None 写进去是真错误）；推断槽的类型跟随写入值，可以整值替换。
/// 判据只能来自源码——`v -> name` 没有注解，`v: T -> name` 有——而 kvspace 里
/// 的槽只存类型、不存「这类型是谁给的」，所以由 layout 显式落一份。
pub const SEG_INFERRED: &str = "inferred";
/// `/lib/<func>/‥crash_resume` → `ALWAYS_RESUMABLE` / `CONDITIONALLY_RESUMABLE:<起点>`
/// / `NOT_RESUMABLE:<原因>`。由 `layout/src/crash_resume.rs` 判定，随 layout 产物外送。
///
/// 只给 dump 与驱动读，**不参与代码生成**。
pub const SEG_CRASH_RESUME: &str = "crash_resume";
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
