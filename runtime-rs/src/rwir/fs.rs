//! rwir `/lib/internet/fs`：宿主文件系统读取，把文件字节引进 kvspace。
//!   internet/fs·size(p) -> n            p 的字节大小（缺失/不可读 = -1）
//!   internet/fs·read(p, start, offset) -> raw   从 start 起读 offset 字节，返回 []uint8
//! 配套的 `xv·reinterpret(raw, "[]char/utf8") -> s` 是 runtime-c 的 native builtin（xv 家族），
//! body 字节原样、整个 kindexpr 换成传入的。三者串起「文件 → 字符串」：size 定长 → read 取字节 →
//! reinterpret 成 []char/utf8 字符串。

use std::io::{Read, Seek, SeekFrom};

use crate::engine::Engine;

/// internet/fs·size(p) -> n：p 的字节大小，不可读回 -1。
pub fn size(eng: &Engine, pc: &str) {
    let path = eng.read0(pc);
    let n = std::fs::metadata(&path).map(|m| m.len() as i64).unwrap_or(-1);
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
