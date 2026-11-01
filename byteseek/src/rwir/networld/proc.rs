//! lib `networld/proc` —— 外部进程。rwir `networld/proc·exec`：同步执行外部命令到结束。
//!   networld/proc·exec(args, envs) -> exitcode, stdout, stderr
//! args/envs 均为 stringkeymap（成员 ·[i] 为 []char/utf32）：args 首成员=可执行文件、
//! 其余为参数；envs 每成员一条 "K=V"，空 = 继承父进程环境、非空 = 完整替换（execve 语义）。
//! exitcode：uint8。正常退出=退出码(0-255)；被信号终止=128+signo；spawn 失败/无 argv=127。
//! Bound `stdout`/`stderr` slots capture via pipes into `/tmp/byteseek-{euid}/{id}/{stdout,stderr}`
//! and write `@[]uint8` handles (`/networld/{host}/proc/{id}/{stream}`, `id = {pid}-{n}`);
//! unbound slots inherit parent stdio, and capture files persist across restarts.

use std::ffi::OsStr;
use std::io::Write;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::{DirBuilderExt, MetadataExt, OpenOptionsExt};
use std::os::unix::process::ExitStatusExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

use crate::engine::{Engine, KvResult};
use crate::ffi::*;

const SEP: &str = "·";

pub fn exec(eng: &Engine, pc: &str) -> KvResult {
    let args = read_str_list(eng, &read_path(eng, pc, 0));
    let envs = read_str_list(eng, &read_path(eng, pc, 1));
    let out_slot = eng.write_at(pc, 1);
    let err_slot = eng.write_at(pc, 2);
    let r = run(&args, &envs, !out_slot.is_empty(), !err_slot.is_empty());
    eng.set_xvalue_wire(&eng.write0(pc), "uint8", &[r.code], &[])?;
    if out_slot.is_empty() && err_slot.is_empty() {
        return Ok(());
    }
    // Share one capture directory across stdout and stderr of the same execution.
    let (id, dir) = new_capture_dir(&capture_root()?, r.pid)?;
    for (slot, stream, bytes) in [(&out_slot, "stdout", &r.out), (&err_slot, "stderr", &r.err)] {
        if !slot.is_empty() {
            write_handle(eng, slot, &id, &dir, stream, bytes)?;
        }
    }
    Ok(())
}

struct Run {
    code: u8,
    pid: u32,
    out: Vec<u8>,
    err: Vec<u8>,
}

fn run(args: &[String], envs: &[String], cap_out: bool, cap_err: bool) -> Run {
    let Some(prog) = args.first() else {
        return Run {
            code: 127,
            pid: 0,
            out: Vec::new(),
            err: Vec::new(),
        };
    };
    let mut cmd = Command::new(OsStr::from_bytes(prog.as_bytes()));
    for a in &args[1..] {
        cmd.arg(OsStr::from_bytes(a.as_bytes()));
    }
    if !envs.is_empty() {
        cmd.env_clear();
        for e in envs {
            if let Some((k, v)) = e.split_once('=') {
                cmd.env(
                    OsStr::from_bytes(k.as_bytes()),
                    OsStr::from_bytes(v.as_bytes()),
                );
            }
        }
    }
    cmd.stdout(if cap_out {
        Stdio::piped()
    } else {
        Stdio::inherit()
    });
    cmd.stderr(if cap_err {
        Stdio::piped()
    } else {
        Stdio::inherit()
    });
    let child = match cmd.spawn() {
        Ok(c) => c,
        Err(_) => {
            return Run {
                code: 127,
                pid: 0,
                out: Vec::new(),
                err: Vec::new(),
            }
        }
    };
    let pid = child.id();
    match child.wait_with_output() {
        Ok(o) => {
            let code = o
                .status
                .code()
                .map(|c| c as u8)
                .unwrap_or_else(|| (128 + o.status.signal().unwrap_or(0)) as u8);
            Run {
                code,
                pid,
                out: o.stdout,
                err: o.stderr,
            }
        }
        Err(_) => Run {
            code: 127,
            pid,
            out: Vec::new(),
            err: Vec::new(),
        },
    }
}

/// Persist captured bytes (exclusive 0600 file) and write the `@[]uint8` handle to `slot`.
/// Fails the op if writing the file fails so no dangling handle is stored.
fn write_handle(
    eng: &Engine,
    slot: &str,
    id: &str,
    dir: &Path,
    stream: &str,
    bytes: &[u8],
) -> KvResult {
    let path = dir.join(stream);
    std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(&path)
        .and_then(|mut f| f.write_all(bytes))
        .map_err(|e| format!("capture {}: {e}", path.display()))?;
    let locator = format!("/networld/{}/proc/{id}/{stream}", super::hostname());
    eng.set_ext_handle(slot, "[]uint8", &locator)
}

/// Per-user private capture root `/tmp/byteseek-{euid}` (avoids `/tmp` symlink/squatting
/// attacks while keeping a fixed path across restarts).
fn capture_root() -> KvResult<PathBuf> {
    let root = capture_root_path();
    ensure_private_dir(&root, true)?;
    Ok(root)
}

fn capture_root_path() -> PathBuf {
    PathBuf::from(format!("/tmp/byteseek-{}", unsafe { libc::geteuid() }))
}

/// Verify `path` is a real directory (via `lstat`, no symlinks) owned by `euid` with mode 0700;
/// creates it with mode 0700 when `create` is true.
fn ensure_private_dir(path: &Path, create: bool) -> KvResult {
    if create {
        match std::fs::DirBuilder::new().mode(0o700).create(path) {
            Ok(()) => {}
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => {}
            Err(e) => return Err(format!("create {}: {e}", path.display())),
        }
    }
    let uid = unsafe { libc::geteuid() };
    let m = std::fs::symlink_metadata(path).map_err(|e| format!("{}: {e}", path.display()))?;
    if !m.file_type().is_dir() || m.uid() != uid || m.mode() & 0o077 != 0 {
        return Err(format!(
            "{} is not a private directory of uid {uid} (symlink, foreign owner, or group/other \
             permissions); refusing to use it for captures",
            path.display()
        ));
    }
    Ok(())
}

/// Allocate a unique `{pid}-{n}` capture directory via exclusive `mkdir`, avoiding PID-reuse collisions.
fn new_capture_dir(root: &Path, pid: u32) -> KvResult<(String, PathBuf)> {
    let mut n = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0);
    for _ in 0..64 {
        let id = format!("{pid}-{n}");
        let dir = root.join(&id);
        match std::fs::DirBuilder::new().mode(0o700).create(&dir) {
            Ok(()) => return Ok((id, dir)),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => n = n.wrapping_add(1),
            Err(e) => return Err(format!("create {}: {e}", dir.display())),
        }
    }
    Err(format!("no free capture id for pid {pid} under {}", root.display()))
}

/// Accept only `{digits}-{digits}` capture IDs to prevent path traversal.
fn valid_capture_id(id: &str) -> bool {
    let digits = |s: &str| !s.is_empty() && s.len() <= 20 && s.bytes().all(|b| b.is_ascii_digit());
    id.split_once('-').is_some_and(|(p, n)| digits(p) && digits(n))
}

/// Materialize `@` handle `/networld/{host}/proc/{id}/{stream}` from the validated private capture dir.
pub fn resolve_read(locator: &str) -> Vec<u8> {
    let parts: Vec<&str> = locator.trim_start_matches('/').split('/').collect();
    let ["networld", host, "proc", id, stream] = parts.as_slice() else {
        return Vec::new();
    };
    if host.is_empty() || !valid_capture_id(id) || !matches!(*stream, "stdout" | "stderr") {
        return Vec::new();
    }
    let root = capture_root_path();
    if ensure_private_dir(&root, false).is_err() {
        return Vec::new();
    }
    std::fs::read(root.join(id).join(stream)).unwrap_or_default()
}

/// 取读参 idx 的容器 KV 路径（ResolveRead 对数组只返首元素，容器须用 ResolveReadPath）。
fn read_path(eng: &Engine, pc: &str, idx: i32) -> String {
    take(unsafe { kvlangRwirextResolveReadPath(eng.kv, cs(pc).as_ptr(), idx) })
}

/// stringkeymap 容器路径 p → 按坐标段 ·[i] 数值升序取各成员字符串（get_kv 解 char/utf32）。
fn read_str_list(eng: &Engine, path: &str) -> Vec<String> {
    if path.is_empty() {
        return Vec::new();
    }
    let mut idxs: Vec<usize> = eng
        .list_kv(&format!("{path}{SEP}"))
        .iter()
        .filter_map(|n| n.trim_start_matches('[').trim_end_matches(']').parse().ok())
        .collect();
    idxs.sort_unstable();
    idxs.iter()
        .map(|i| eng.get_kv(&format!("{path}{SEP}[{i}]")))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::PermissionsExt;

    fn scratch(tag: &str) -> PathBuf {
        let p = std::env::temp_dir().join(format!("byteseek-proc-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&p);
        let _ = std::fs::remove_file(&p);
        p
    }

    #[test]
    fn capture_id_shape() {
        assert!(valid_capture_id("1234-1730000000000000000"));
        for bad in ["", "1234", "-1", "1-", "..", "1-2/..", "1-2-3", "a-1", "1-../x", " 1-2"] {
            assert!(!valid_capture_id(bad), "{bad:?}");
        }
    }

    #[test]
    fn private_dir_checks() {
        let d = scratch("dir");
        ensure_private_dir(&d, true).unwrap();
        let m = std::fs::symlink_metadata(&d).unwrap();
        assert_eq!(m.mode() & 0o777, 0o700);
        ensure_private_dir(&d, false).unwrap();
        // Reject group/other permissions.
        std::fs::set_permissions(&d, std::fs::Permissions::from_mode(0o775)).unwrap();
        assert!(ensure_private_dir(&d, true).is_err());
        std::fs::set_permissions(&d, std::fs::Permissions::from_mode(0o700)).unwrap();
        // Reject symlinks even when pointing to a private dir (lstat).
        let l = scratch("link");
        std::os::unix::fs::symlink(&d, &l).unwrap();
        assert!(ensure_private_dir(&l, true).is_err());
        // Reject missing dir when create=false.
        assert!(ensure_private_dir(&scratch("missing"), false).is_err());
        std::fs::remove_file(&l).unwrap();
        std::fs::remove_dir_all(&d).unwrap();
    }

    #[test]
    fn capture_ids_never_collide_for_same_pid() {
        let d = scratch("ids");
        ensure_private_dir(&d, true).unwrap();
        let (a, da) = new_capture_dir(&d, 42).unwrap();
        let (b, db) = new_capture_dir(&d, 42).unwrap();
        assert_ne!(a, b);
        assert!(valid_capture_id(&a) && valid_capture_id(&b));
        assert!(a.starts_with("42-") && da.is_dir() && db.is_dir());
        std::fs::remove_dir_all(&d).unwrap();
    }

    #[test]
    fn resolve_read_rejects_bad_locators() {
        for bad in [
            "/networld/h/proc/../../etc/passwd",
            "/networld/h/proc/1-2/../x",
            "/networld/h/proc/1/stdout",
            "/networld/h/proc/1-2/passwd",
            "/networld//proc/1-2/stdout",
            "/elsewhere/h/proc/1-2/stdout",
        ] {
            assert!(resolve_read(bad).is_empty(), "{bad}");
        }
    }
}
