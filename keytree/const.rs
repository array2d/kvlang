//! Path constants — matching keytree/const.go and keytree/vthread.go.

pub const PATHS_SEP: &str = "/";
pub const PATH_SEG_LIB: &str = "lib";
pub const PATH_SEG_VTHREAD: &str = "vthread";
pub const RUNTIME_MEMBER_SEP: &str = "‥"; // U+2025, matching Go
pub const VTHREAD_ROOT: &str = "/vthread";

pub const SEG_PC: &str = "pc";
pub const SEG_STATUS: &str = "status";
pub const SEG_CALLPC: &str = "callpc";
pub const SEG_LIB: &str = "lib";

pub fn lib_path(name: &str) -> String { format!("/{}/{}", PATH_SEG_LIB, name) }
pub fn vthread(vtid: &str) -> String { format!("{}/{}", VTHREAD_ROOT, vtid) }
fn vt_member(vtid: &str, seg: &str) -> String { format!("{}/{}{}", vthread(vtid), RUNTIME_MEMBER_SEP, seg) }
pub fn vthread_pc(vtid: &str) -> String { vt_member(vtid, SEG_PC) }
pub fn vthread_status(vtid: &str) -> String { vt_member(vtid, SEG_STATUS) }
