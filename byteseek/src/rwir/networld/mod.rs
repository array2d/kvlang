//! lib `networld` —— 与外部世界（本机及网络）交互的 rwir 命名空间，按 kvlang lib 嵌套组织：
//!   networld/proc : networld/proc·exec              外部进程执行 + stdout/stderr @ 句柄
//!   networld/fs   : networld/fs·size / networld/fs·read   宿主文件系统读取
//! `/networld/{host}` 是本机在该命名空间下的身份，host 取裸 hostname。

pub mod fs;
pub mod proc;

/// 裸 hostname（gethostname）：本机在 /networld 命名空间下的标识。单机场景足够；
/// 多机唯一性由上层保证。
pub fn hostname() -> String {
    let mut buf = [0u8; 256];
    let rc = unsafe { libc::gethostname(buf.as_mut_ptr() as *mut libc::c_char, buf.len()) };
    if rc != 0 {
        return "localhost".to_string();
    }
    let n = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    String::from_utf8_lossy(&buf[..n]).into_owned()
}
