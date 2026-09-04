//! KV 路径与成员分隔符统一管理（对齐 keytree/const.go、entry.go、frame.go、
//! member.go、vthread.go、sys.go）。所有构造 KV 路径的地方均须使用本模块常量。

// ── 常量 ─────────────────────────────────────────────────────────────

pub const MEMBER_SEP: &str = "·"; // 成员访问分隔符（U+00B7 中点号）：释放 '.' 供小数 key 使用
pub const RUNTIME_MEMBER_SEP: &str = "\u{2025}"; // U+2025，运行时保留字段前缀
pub const PATH_SEG_SEP: &str = "/"; // 路径分隔符
pub const PATH_SEG_LIB: &str = "lib"; // /lib
pub const PATH_SEG_VTHREAD: &str = "vthread"; // /vthread

pub const SEG_RO: &str = "ro";
pub const SEG_RPARAM: &str = "rparam";
pub const SEG_WPARAM: &str = "wparam";
pub const SEG_PC: &str = "pc";
pub const SEG_CALLPC: &str = "callpc";
pub const SEG_RETURNPC: &str = "returnpc";
pub const SEG_STATUS: &str = "status";
pub const SEG_CTIME: &str = "ctime";
pub const SEG_DEBUGGER: &str = "debugger";

pub const SEG_LIB: &str = "\u{2025}lib"; // 帧 extindex 标记
pub const SEG_LABELS: &str = "labels"; // /lib/<func>/‥labels/<name> → irseq
pub const SRC_EXT: &str = ".src"; // 函数源码文件后缀

pub const LIB_ROOT: &str = "/lib";
pub const RWIR_ROOT: &str = "/lib";
pub const VTHREAD_ROOT: &str = "/vthread";

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

// ── 帧路径 ───────────────────────────────────────────────────────────

fn frame_member(root: &str, seg: &str) -> String {
    format!("{}{}{}", stack(root), RUNTIME_MEMBER_SEP, seg)
}

pub fn stack(root: &str) -> String {
    format!("{}/", root.trim_end_matches(PATH_SEG_SEP))
}

pub fn frame_ro(root: &str) -> String {
    frame_member(root, SEG_RO)
}

pub fn call_pc(root: &str) -> String {
    frame_member(root, SEG_CALLPC)
}

pub fn return_pc(root: &str) -> String {
    frame_member(root, SEG_RETURNPC)
}

/// 从 PC 提取帧根（pc 形如 /vthread/42/[3,0] 或 /vthread/42/[3]/[5,0]）。
pub fn frame_root(pc: &str) -> &str {
    if let Some(idx) = pc.rfind("/[") {
        &pc[..idx]
    } else {
        panic!("frame_root: pc has no /[coord] segment: {pc:?}")
    }
}

pub fn entry_pc(root: &str) -> String {
    format!("{}/[1,0]", root.trim_end_matches(PATH_SEG_SEP))
}

pub fn is_entry_pc(pc: &str) -> bool {
    pc.ends_with("/[1,0]")
}

pub fn irseq_pc(frame_root: &str, irseq: i32) -> String {
    if irseq < 0 {
        panic!("irseq_pc: irseq {irseq} < 0");
    }
    format!("{}/[{irseq},0]", frame_root.trim_end_matches(PATH_SEG_SEP))
}

// ── 成员 ─────────────────────────────────────────────────────────────

pub fn member(base: &str, name: &str) -> String {
    format!("{base}{MEMBER_SEP}{name}")
}

// ── /vthread ─────────────────────────────────────────────────────────

pub fn v_thread(vtid: &str) -> String {
    format!("{VTHREAD_ROOT}/{vtid}")
}

fn vt_member(vtid: &str, seg: &str) -> String {
    format!("{}/{}{}", v_thread(vtid), RUNTIME_MEMBER_SEP, seg)
}

pub fn v_thread_pc(vtid: &str) -> String {
    vt_member(vtid, SEG_PC)
}

pub fn v_thread_status(vtid: &str) -> String {
    vt_member(vtid, SEG_STATUS)
}

pub fn v_thread_ctime(vtid: &str) -> String {
    vt_member(vtid, SEG_CTIME)
}

pub fn v_thread_debugger(vtid: &str) -> String {
    vt_member(vtid, SEG_DEBUGGER)
}

pub fn v_thread_at(vtid: &str, key: &str) -> String {
    format!("{}/{key}", v_thread(vtid))
}

/// 从 pc（/vthread/<vtid>/...）提取 vtid。
pub fn vtid_from_pc(pc: &str) -> String {
    let prefix = format!("{VTHREAD_ROOT}/");
    if !pc.starts_with(&prefix) {
        return String::new();
    }
    let rest = &pc[prefix.len()..];
    match rest.find(PATH_SEG_SEP) {
        Some(i) => rest[..i].to_string(),
        None => rest.to_string(),
    }
}
