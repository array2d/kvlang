//! KV path constants — identical to keytree/const.go and keytree/const.h.

pub const SYS_ROOT: &str = "/sys";
pub const SYS_VM: &str = "/sys/vm";
pub const SYS_VT: &str = "/sys/vthread";
pub const SYS_LIB: &str = "/sys/lib";
pub const LIB_ROOT: &str = "/lib";
pub const VT_ROOT: &str = "/vthread";
pub const FRAME_PC: &str = ".pc";
pub const FRAME_STATUS: &str = ".status";
pub const FRAME_RETVAL: &str = ".retval";
pub const FRAME_ERR: &str = ".err";
pub const FRAME_DEBUG: &str = ".debugger";
pub const FRAME_X: &str = ".x";
pub const FRAME_RPARAM: &str = ".rparam";
pub const FRAME_WPARAM: &str = ".wparam";

pub fn vt_path(vtid: &str) -> String { format!("/vthread/{vtid}") }
pub fn vt_pc(vtid: &str) -> String { format!("/vthread/{vtid}/.pc") }
pub fn lib_func(pkg: &str, name: &str) -> String { format!("/lib/{pkg}.{name}") }
pub fn frame_local(root: &str, slot: &str) -> String { format!("{root}.x/{slot}") }
