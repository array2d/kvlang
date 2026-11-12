
use std::io::{Read, Seek, SeekFrom, Write};

use crate::engine::{Engine, KvResult};

pub fn size(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let n = std::fs::metadata(&path)
        .map(|m| m.len() as i64)
        .unwrap_or(-1);
    eng.set_xvalue_wire(&eng.write0(pc), "int64", &n.to_le_bytes(), &[])
}

pub fn read(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let start = eng.read_i64(pc, 1);
    let len = eng.read_i64(pc, 2);
    let bytes = read_slice(&path, start, len)?;
    let n = i32::try_from(bytes.len()).map_err(|_| format!("{} bytes > i32::MAX", bytes.len()))?;
    eng.set_xvalue_wire(&eng.write0(pc), "uint8", &bytes, &[n])
}

pub fn write(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let data = eng.read_bytes(pc, 1);
    let n = std::fs::write(&path, &data)
        .map(|_| data.len() as i64)
        .unwrap_or(-1);
    eng.set_xvalue_wire(&eng.write0(pc), "int64", &n.to_le_bytes(), &[])
}

pub fn append(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let data = eng.read_bytes(pc, 1);
    let n = append_bytes(&path, &data)
        .map(|_| data.len() as i64)
        .unwrap_or(-1);
    eng.set_xvalue_wire(&eng.write0(pc), "int64", &n.to_le_bytes(), &[])
}

pub fn rename(eng: &Engine, pc: &str) -> KvResult {
    let src = eng.read0(pc);
    let dst = eng.read_at(pc, 1);
    let code = std::fs::rename(&src, &dst).map(|_| 0i64).unwrap_or(-1);
    eng.set_xvalue_wire(&eng.write0(pc), "int64", &code.to_le_bytes(), &[])
}

pub fn list(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let mut names: Vec<String> = match std::fs::read_dir(&path) {
        Ok(rd) => rd
            .filter_map(|e| e.ok())
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect(),
        Err(_) => Vec::new(),
    };
    names.sort();
    eng.set_str_list(&eng.write0(pc), &names)
}

pub fn del(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let r = match std::fs::symlink_metadata(&path) {
        Ok(m) if m.is_dir() => std::fs::remove_dir(&path), 
        Ok(_) => std::fs::remove_file(&path),
        Err(e) => Err(e),
    };
    let code: i64 = if r.is_ok() { 0 } else { -1 };
    eng.set_xvalue_wire(&eng.write0(pc), "int64", &code.to_le_bytes(), &[])
}

pub fn mkdir(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let code: i64 = if std::fs::create_dir_all(&path).is_ok() {
        0
    } else {
        -1
    };
    eng.set_xvalue_wire(&eng.write0(pc), "int64", &code.to_le_bytes(), &[])
}

pub fn exists(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let b = std::path::Path::new(&path).exists();
    eng.set_xvalue_wire(&eng.write0(pc), "bool", &[b as u8], &[])
}

pub fn isutf8(eng: &Engine, pc: &str) -> KvResult {
    let path = eng.read0(pc);
    let b = std::fs::read(&path)
        .map(|bytes| std::str::from_utf8(&bytes).is_ok())
        .unwrap_or(false);
    eng.set_xvalue_wire(&eng.write0(pc), "bool", &[b as u8], &[])
}

const MAX_READ: u64 = i32::MAX as u64;

fn read_slice(path: &str, start: i64, len: i64) -> KvResult<Vec<u8>> {
    if len <= 0 {
        return Ok(Vec::new());
    }
    let Ok(mut f) = std::fs::File::open(path) else {
        return Ok(Vec::new());
    };
    let start = start.max(0) as u64;
    let mut want = len as u64;
    let regular = match f.metadata() {
        Ok(m) if m.is_file() => {
            want = want.min(m.len().saturating_sub(start));
            true
        }
        Ok(_) => false,
        Err(_) => return Ok(Vec::new()),
    };
    if want > MAX_READ {
        return Err(format!(
            "read {path}: {want} bytes requested from offset {start} exceeds the {MAX_READ}-byte value limit"
        ));
    }
    if want == 0 && regular {
        return Ok(Vec::new());
    }
    if start > 0 && f.seek(SeekFrom::Start(start)).is_err() {
        return Ok(Vec::new());
    }
    
    let mut buf = Vec::with_capacity(if regular { want as usize } else { 0 });
    match f.take(want).read_to_end(&mut buf) {
        Ok(_) => Ok(buf),
        Err(_) => Ok(Vec::new()),
    }
}

fn append_bytes(path: &str, data: &[u8]) -> std::io::Result<()> {
    let mut f = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)?;
    f.write_all(data)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_slice_bounds_kv_controlled_len() {
        let p = std::env::temp_dir().join(format!("byteseek-fs-read-{}", std::process::id()));
        std::fs::write(&p, b"hello kvlang").unwrap();
        let ps = p.to_str().unwrap();
        
        assert_eq!(read_slice(ps, 6, 1 << 62).unwrap(), b"kvlang");
        assert_eq!(read_slice(ps, 0, i64::MAX).unwrap(), b"hello kvlang");
        assert_eq!(read_slice(ps, 0, 5).unwrap(), b"hello");
        assert_eq!(read_slice(ps, -3, 5).unwrap(), b"hello"); 
        assert!(read_slice(ps, 100, 5).unwrap().is_empty()); 
        assert!(read_slice(ps, 0, 0).unwrap().is_empty());
        assert!(read_slice(ps, 0, -1).unwrap().is_empty());
        std::fs::remove_file(&p).unwrap();
        
        assert!(read_slice(ps, 0, 1 << 62).unwrap().is_empty());
        
        assert!(read_slice("/dev/zero", 0, 1 << 62).is_err());
        assert_eq!(read_slice("/dev/zero", 0, 3).unwrap(), [0, 0, 0]);
    }
}
