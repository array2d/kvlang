//! lib `internet/fs` —— 宿主文件系统操作（对齐 POSIX），把文件世界接进 kvspace。
//!   internet/fs·size(p) -> n                p 的字节大小（缺失/不可读 = -1）
//!   internet/fs·read(p, start, offset) -> raw   从 start 起读 offset 字节，返回 []uint8
//!   internet/fs·write(p, data) -> n         把 data([]uint8) 覆盖写入 p（创建/截断），返回写入字节数（失败 -1）
//!   internet/fs·append(p, data) -> n        把 data([]uint8) 追加到 p 末尾，返回写入字节数（失败 -1）
//!   internet/fs·list(p) -> names            列目录 p 的成员名（[]stringkeymap，名字序）；p 必须是目录
//!   internet/fs·del(p) -> code              删除 p，0 成功/-1 失败；p 必须是文件或空目录（非空目录失败）
//!   internet/fs·mkdir(p) -> code            创建目录 p（含缺失父级，幂等），0 成功/-1 失败
//!   internet/fs·exists(p) -> b              p 是否存在（bool）
//! 配套 `xv·reinterpret(raw, "[]char/utf8")`（runtime-c native，body 原样、换 kindexpr）串起
//! 「文件 → 字符串」；反向「字符串 → 文件」由 reinterpret 成 []uint8 后 write。

use std::io::{Read, Seek, SeekFrom, Write};

use crate::engine::Engine;

/// internet/fs·size(p) -> n：p 的字节大小，不可读回 -1。
pub fn size(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let n = std::fs::metadata(&path)
        .map(|m| m.len() as i64)
        .unwrap_or(-1);
    eng.set_tlv_encoded(&eng.write0(pc), "int64", &n.to_le_bytes(), &[]);
}

/// internet/fs·read(p, start, offset) -> raw：从 start 起读 offset 字节（[]uint8）。
pub fn read(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let start = eng.read_i64(pc, 1);
    let len = eng.read_i64(pc, 2);
    let bytes = read_slice(&path, start, len);
    eng.set_tlv_encoded(&eng.write0(pc), "uint8", &bytes, &[bytes.len() as i32]);
}

/// internet/fs·write(p, data) -> n：把 data 覆盖写入 p（创建/截断），返回写入字节数（失败 -1）。
pub fn write(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let data = eng.read_bytes(pc, 1);
    let n = std::fs::write(&path, &data)
        .map(|_| data.len() as i64)
        .unwrap_or(-1);
    eng.set_tlv_encoded(&eng.write0(pc), "int64", &n.to_le_bytes(), &[]);
}

/// internet/fs·append(p, data) -> n：把 data 追加到 p 末尾，返回写入字节数（失败 -1）。
pub fn append(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let data = eng.read_bytes(pc, 1);
    let n = append_bytes(&path, &data)
        .map(|_| data.len() as i64)
        .unwrap_or(-1);
    eng.set_tlv_encoded(&eng.write0(pc), "int64", &n.to_le_bytes(), &[]);
}

/// internet/fs·list(p) -> names：列目录 p 的成员名（名字序）。p 必须是目录，否则空列表。
pub fn list(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let mut names: Vec<String> = match std::fs::read_dir(&path) {
        Ok(rd) => rd
            .filter_map(|e| e.ok())
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect(),
        Err(_) => Vec::new(),
    };
    names.sort();
    eng.set_str_list(&eng.write0(pc), &names);
}

/// internet/fs·del(p) -> code：删除 p，0 成功/-1 失败。p 必须是文件或空目录（非空目录失败）。
pub fn del(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let r = match std::fs::symlink_metadata(&path) {
        Ok(m) if m.is_dir() => std::fs::remove_dir(&path), // 仅空目录成功，非空报错
        Ok(_) => std::fs::remove_file(&path),
        Err(e) => Err(e),
    };
    let code: i64 = if r.is_ok() { 0 } else { -1 };
    eng.set_tlv_encoded(&eng.write0(pc), "int64", &code.to_le_bytes(), &[]);
}

/// internet/fs·mkdir(p) -> code：创建目录 p（含缺失父级，幂等），0 成功/-1 失败。
pub fn mkdir(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let code: i64 = if std::fs::create_dir_all(&path).is_ok() {
        0
    } else {
        -1
    };
    eng.set_tlv_encoded(&eng.write0(pc), "int64", &code.to_le_bytes(), &[]);
}

/// internet/fs·exists(p) -> b：p 是否存在。
pub fn exists(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let b = std::path::Path::new(&path).exists();
    eng.set_tlv_encoded(&eng.write0(pc), "bool", &[b as u8], &[]);
}

fn read_slice(path: &str, start: i64, len: i64) -> Vec<u8> {
    let Ok(mut f) = std::fs::File::open(path) else {
        return Vec::new();
    };
    if start > 0 {
        f.seek(SeekFrom::Start(start as u64)).ok();
    }
    let mut buf = vec![0u8; len.max(0) as usize];
    let n = f.read(&mut buf).unwrap_or(0);
    buf.truncate(n);
    buf
}

fn append_bytes(path: &str, data: &[u8]) -> std::io::Result<()> {
    let mut f = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)?;
    f.write_all(data)
}
