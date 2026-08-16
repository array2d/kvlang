//! Path constants — matching keytree/const.go, keytree/frame.go, keytree/vthread.go.

pub const PATH_SEP: &str = "/";
pub const PATH_SEG_LIB: &str = "lib";
pub const PATH_SEG_VTHREAD: &str = "vthread";
pub const RUNTIME_MEMBER_SEP: &str = "\u{2025}"; // U+2025, matching Go RuntimeMemberSep
pub const MEMBER_SEP: &str = ".";

// runtime reserved segments (keytree/const.go)
const SEG_LIB: &str = "\u{2025}lib";         // frame marker
const SEG_PC: &str = "pc";
const SEG_STATUS: &str = "status";
const SEG_CALLPC: &str = "callpc";
const SEG_RETURNPC: &str = "returnpc";

// ── path builders ──

pub fn lib_path(name: &str) -> String { format!("/{}/{}", PATH_SEG_LIB, name) }

pub fn lib_func_dir(pkg: &str, name: &str) -> String {
    if pkg.is_empty() { format!("/{}/{}", PATH_SEG_LIB, name) }
    else { format!("/{}/{}.{}", PATH_SEG_LIB, pkg, name) }
}

pub fn vthread_root(vtid: &str) -> String { format!("/{}/{}", PATH_SEG_VTHREAD, vtid) }

fn vt_member(vtid: &str, seg: &str) -> String {
    format!("{}/{}{}", vthread_root(vtid), RUNTIME_MEMBER_SEP, seg)
}

pub fn vthread_pc(vtid: &str) -> String { vt_member(vtid, SEG_PC) }
pub fn vthread_status(vtid: &str) -> String { vt_member(vtid, SEG_STATUS) }

// frame-level keys (keytree/frame.go)
pub fn frame_root(pc: &str) -> &str {
    if let Some(idx) = pc.rfind(&format!("{}{}", PATH_SEP, "[")) {
        &pc[..idx]
    } else {
        pc
    }
}

pub fn entry_pc(root: &str) -> String {
    let trimmed = root.trim_end_matches(PATH_SEP);
    format!("{}/[1,0]", trimmed)
}

pub fn frame_stack(root: &str) -> String {
    let trimmed = root.trim_end_matches(PATH_SEP);
    format!("{}/", trimmed)
}

fn frame_member(root: &str, seg: &str) -> String {
    format!("{}{}{}", frame_stack(root), RUNTIME_MEMBER_SEP, seg)
}

pub fn frame_lib(root: &str) -> String { frame_member(root, "lib") }
pub fn frame_callpc(root: &str) -> String { frame_member(root, SEG_CALLPC) }
pub fn frame_returnpc(root: &str) -> String { frame_member(root, SEG_RETURNPC) }

// ── XValue kind constants (matching kvspace-go/const.go) ──

pub const KIND_NONE: &str = "";
pub const KIND_BOOL: &str = "bool";
pub const KIND_INT8: &str = "int8";
pub const KIND_INT16: &str = "int16";
pub const KIND_INT32: &str = "int32";
pub const KIND_INT64: &str = "int64";
pub const KIND_UINT8: &str = "uint8";
pub const KIND_UINT16: &str = "uint16";
pub const KIND_UINT32: &str = "uint32";
pub const KIND_UINT64: &str = "uint64";
pub const KIND_FLOAT32: &str = "float32";
pub const KIND_FLOAT64: &str = "float64";
pub const KIND_STRING: &str = "string";
pub const KIND_BYTES: &str = "bytes";
pub const KIND_INDEX: &str = "index";
pub const KIND_LINKINDEX: &str = "linkindex";
pub const KIND_EXTINDEX: &str = "extindex";
pub const KIND_RWIR: &str = "rwir";
pub const KIND_RWFUNC: &str = "rwfunc";
pub const KIND_SCOPE: &str = "scope";

// ── opcode constants (matching rwir/rwir.go OpCall etc.) ──

pub const OP_CALL: &str = "call";
pub const OP_RETURN: &str = "return";
pub const OP_GOTO: &str = "goto";
pub const OP_BR: &str = "br";
