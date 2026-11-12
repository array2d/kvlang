
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;

mod ffi {
    use super::*;
    extern "C" {
        pub fn kvspaceConnect(dsn: *const c_char) -> *mut std::ffi::c_void;
        pub fn kvspaceClose(h: *mut std::ffi::c_void);
        pub fn kvspaceXvalueInProcessAddress(h: *mut std::ffi::c_void) -> *mut u8;
        pub fn kvspaceResolveOffset(
            h: *mut std::ffi::c_void,
            key: *const c_char,
            parent_key_anchor: u64,
            out_value_offset: *mut u64,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        pub fn kvspaceRealloc(
            h: *mut std::ffi::c_void,
            key: *const c_char,
            body_length: u64,
            attr_length: u64,
            out_value_offset: *mut u64,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        pub fn kvspaceFree(
            h: *mut std::ffi::c_void,
            key: *const c_char,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        pub fn kvspaceCp(
            h: *mut std::ffi::c_void,
            old_key: *const c_char,
            new_key: *const c_char,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        pub fn kvspaceMkindex(
            h: *mut std::ffi::c_void,
            key: *const c_char,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        pub fn kvspaceDeltree(
            h: *mut std::ffi::c_void,
            key: *const c_char,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        pub fn kvspaceCpdir(
            h: *mut std::ffi::c_void,
            source_key: *const c_char,
            destination_key: *const c_char,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        pub fn kvspaceListlen(
            h: *mut std::ffi::c_void,
            key: *const c_char,
            out_count: *mut i64,
            out_max_name_length: *mut u32,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        pub fn kvspaceList(
            h: *mut std::ffi::c_void,
            key: *const c_char,
            out_names: *mut c_char,
            out_capacity: u32,
            out_length: *mut u32,
            out_count: *mut u32,
            out_generation: *mut u64,
            err: *mut c_char,
            err_cap: u32,
        ) -> i32;
        
        pub fn kvlangXvalueHeadNewScalar(
            raw: *const u8,
            raw_len: u32,
            langtype: *const c_char,
            out: *mut *mut u8,
            out_len: *mut u64,
        ) -> i32;
        pub fn kvlangXvalueHeadNewSlack(
            data: *const u8,
            data_len: u64,
            langtype: *const c_char,
            out: *mut *mut u8,
            out_len: *mut u64,
        ) -> i32;
        pub fn kvlangXvalueHeadNewPtr(
            path: *const u8,
            path_len: u32,
            langtype: *const c_char,
            out: *mut *mut u8,
            out_len: *mut u64,
        ) -> i32;
        pub fn kvlangXvalueHeadNewExt(
            locator: *const u8,
            locator_len: u32,
            langtype: *const c_char,
            out: *mut *mut u8,
            out_len: *mut u64,
        ) -> i32;
        pub fn free(p: *mut std::ffi::c_void);
    }
}

pub mod xvalue {
    use super::ffi;
    use super::*;

    pub const REF_INLINE: u8 = 0;
    pub const REF_PTR: u8 = 1;
    pub const REF_EXT: u8 = 2;
    pub const HEAD_LEN: usize = 16;
    pub const PADDING_LEN: usize = 6;
    pub const ATTR_TAG_LANGTYPE: u8 = 1;
    
    pub const BANNED_ATTR_TAG_REALLEN: u8 = 2;
    
    const NO_TYPE: *const c_char = b"\0".as_ptr() as *const c_char;

    fn take(out: *mut u8, total: u64) -> Vec<u8> {
        let bytes = unsafe { std::slice::from_raw_parts(out, total as usize).to_vec() };
        unsafe { ffi::free(out as *mut std::ffi::c_void) };
        bytes
    }

    pub fn int64(v: i64) -> Vec<u8> {
        let mut out: *mut u8 = ptr::null_mut();
        let mut total: u64 = 0;
        let raw = v.to_le_bytes();
        let rc = unsafe {
            ffi::kvlangXvalueHeadNewScalar(raw.as_ptr(), 8, NO_TYPE, &mut out, &mut total)
        };
        if rc != 0 {
            return Vec::new();
        }
        take(out, total)
    }
    pub fn float64(v: f64) -> Vec<u8> {
        let mut out: *mut u8 = ptr::null_mut();
        let mut total: u64 = 0;
        let raw = v.to_le_bytes();
        let rc = unsafe {
            ffi::kvlangXvalueHeadNewScalar(raw.as_ptr(), 8, NO_TYPE, &mut out, &mut total)
        };
        if rc != 0 {
            return Vec::new();
        }
        take(out, total)
    }
    pub fn string(s: &str) -> Vec<u8> {
        let mut out: *mut u8 = ptr::null_mut();
        let mut total: u64 = 0;
        let rc = unsafe {
            ffi::kvlangXvalueHeadNewSlack(s.as_ptr(), s.len() as u64, NO_TYPE, &mut out, &mut total)
        };
        if rc != 0 {
            return Vec::new();
        }
        take(out, total)
    }
    
    pub fn index() -> Vec<u8> {
        let mut out: *mut u8 = ptr::null_mut();
        let mut total: u64 = 0;
        let rc = unsafe {
            ffi::kvlangXvalueHeadNewScalar(ptr::null(), 0, NO_TYPE, &mut out, &mut total)
        };
        if rc != 0 {
            return Vec::new();
        }
        take(out, total)
    }
    
    pub fn link(target: &str) -> Vec<u8> {
        let mut out: *mut u8 = ptr::null_mut();
        let mut total: u64 = 0;
        let rc = unsafe {
            ffi::kvlangXvalueHeadNewPtr(target.as_ptr(), target.len() as u32, NO_TYPE, &mut out, &mut total)
        };
        if rc != 0 {
            return Vec::new();
        }
        take(out, total)
    }
    
    pub fn ext(locator: &str) -> Vec<u8> {
        let mut out: *mut u8 = ptr::null_mut();
        let mut total: u64 = 0;
        let rc = unsafe {
            ffi::kvlangXvalueHeadNewExt(
                locator.as_ptr(),
                locator.len() as u32,
                NO_TYPE,
                &mut out,
                &mut total,
            )
        };
        if rc != 0 {
            return Vec::new();
        }
        take(out, total)
    }

    pub fn from_wire(data: &[u8]) -> (u8, &[u8]) {
        if data.len() < HEAD_LEN {
            return (REF_INLINE, &[]);
        }
        let mut body_len: u64 = 0;
        for (i, b) in data[..8].iter().enumerate() {
            body_len |= (*b as u64) << (8 * i);
        }
        let attrs_len = u16::from_le_bytes([data[8], data[9]]) as usize;
        if data[10..10 + PADDING_LEN].iter().any(|b| *b != 0) {
            return (REF_INLINE, &[]);
        }
        let Ok(blen) = usize::try_from(body_len) else {
            return (REF_INLINE, &[]);
        };
        let Some(a) = HEAD_LEN.checked_add(blen) else {
            return (REF_INLINE, &[]);
        };
        let Some(total) = a.checked_add(attrs_len) else {
            return (REF_INLINE, &[]);
        };
        if total != data.len() {
            return (REF_INLINE, &[]);
        }
        let attr = &data[a..total];
        let mut p = 0usize;
        let mut langtype: &[u8] = &[];
        while p + 3 <= attr.len() {
            let tag = attr[p];
            let rlen = u16::from_le_bytes([attr[p + 1], attr[p + 2]]) as usize;
            if p + 3 + rlen > attr.len() || tag == BANNED_ATTR_TAG_REALLEN {
                return (REF_INLINE, &[]);
            }
            if tag == ATTR_TAG_LANGTYPE {
                langtype = &attr[p + 3..p + 3 + rlen];
            }
            p += 3 + rlen;
        }
        let r#ref = match langtype.first() {
            Some(b'*') => REF_PTR,
            Some(b'@') => REF_EXT,
            _ => REF_INLINE,
        };
        (r#ref, &data[HEAD_LEN..a])
    }
}

const KVSPACE_OK: i32 = 0;
const KVSPACE_MISSING: i32 = -2; 

pub struct KVSpace {
    ptr: *mut std::ffi::c_void,
    path: String,
}

impl KVSpace {
    
    pub fn open(path: &str) -> Result<Self, String> {
        let _ = std::fs::remove_file(path);
        let dsn = CString::new(format!("shm://{path}")).map_err(|e| e.to_string())?;
        let ptr = unsafe { ffi::kvspaceConnect(dsn.as_ptr()) };
        if ptr.is_null() {
            return Err(format!("kvspaceConnect(shm://{path}) failed"));
        }
        Ok(KVSpace { ptr, path: path.to_string() })
    }

    fn base(&self) -> Option<*mut u8> {
        let b = unsafe { ffi::kvspaceXvalueInProcessAddress(self.ptr) };
        (!b.is_null()).then_some(b)
    }

    pub fn get(&self, key: &str) -> Option<Vec<u8>> {
        let ckey = CString::new(key).ok()?;
        let mut off: u64 = 0;
        let mut err = [0 as c_char; 256];
        let rc = unsafe {
            ffi::kvspaceResolveOffset(self.ptr, ckey.as_ptr(), 0, &mut off, err.as_mut_ptr(), 256)
        };
        if rc != KVSPACE_OK {
            return None;
        }
        let base = self.base()?;
        unsafe {
            
            let head = std::slice::from_raw_parts(base.add(off as usize), 16);
            let body_len = u64::from_le_bytes(head[0..8].try_into().ok()?) as usize;
            let attr_len = u16::from_le_bytes(head[8..10].try_into().ok()?) as usize;
            let total = 16 + body_len + attr_len;
            Some(std::slice::from_raw_parts(base.add(off as usize), total).to_vec())
        }
    }

    pub fn set(&self, key: &str, wire: &[u8]) -> Result<(), String> {
        if wire.len() < 16 {
            return Err(format!("set {key}: wire shorter than head"));
        }
        let body_len = u64::from_le_bytes(wire[0..8].try_into().unwrap()) as usize;
        let attr_len = u16::from_le_bytes(wire[8..10].try_into().unwrap()) as usize;
        if 16 + body_len + attr_len > wire.len() {
            return Err(format!("set {key}: wire self-length overruns buffer"));
        }
        let ckey = CString::new(key).map_err(|e| e.to_string())?;
        let mut off: u64 = 0;
        let mut err = [0 as c_char; 256];
        let rc = unsafe {
            ffi::kvspaceRealloc(
                self.ptr,
                ckey.as_ptr(),
                body_len as u64,
                attr_len as u64,
                &mut off,
                err.as_mut_ptr(),
                256,
            )
        };
        if rc != KVSPACE_OK {
            return Err(err_str(&err));
        }
        let base = self.base().ok_or("no in-process value region")?;
        unsafe {
            let dst = base.add(off as usize);
            if body_len > 0 {
                std::ptr::copy_nonoverlapping(wire[16..].as_ptr(), dst.add(16), body_len);
            }
            if attr_len > 0 {
                std::ptr::copy_nonoverlapping(
                    wire[16 + body_len..].as_ptr(),
                    dst.add(16 + body_len),
                    attr_len,
                );
            }
        }
        Ok(())
    }

    pub fn delete(&self, key: &str) -> Result<(), String> {
        let ckey = CString::new(key).map_err(|e| e.to_string())?;
        let mut err = [0 as c_char; 256];
        let rc = unsafe { ffi::kvspaceFree(self.ptr, ckey.as_ptr(), err.as_mut_ptr(), 256) };
        if rc == KVSPACE_OK || rc == KVSPACE_MISSING {
            Ok(())
        } else {
            Err(err_str(&err))
        }
    }

    pub fn deltree(&self, prefix: &str) -> Result<(), String> {
        let cp = CString::new(prefix).map_err(|e| e.to_string())?;
        let mut err = [0 as c_char; 256];
        let rc = unsafe { ffi::kvspaceDeltree(self.ptr, cp.as_ptr(), err.as_mut_ptr(), 256) };
        if rc == KVSPACE_OK { Ok(()) } else { Err(err_str(&err)) }
    }

    pub fn cp(&self, src: &str, dst: &str) -> Result<(), String> {
        let (s, d) = (CString::new(src).unwrap(), CString::new(dst).unwrap());
        let mut err = [0 as c_char; 256];
        let rc = unsafe {
            ffi::kvspaceCp(self.ptr, s.as_ptr(), d.as_ptr(), err.as_mut_ptr(), 256)
        };
        if rc == KVSPACE_OK { Ok(()) } else { Err(err_str(&err)) }
    }

    pub fn cpdir(&self, src: &str, dst: &str) -> Result<(), String> {
        let (s, d) = (CString::new(src).unwrap(), CString::new(dst).unwrap());
        let mut err = [0 as c_char; 256];
        let rc = unsafe {
            ffi::kvspaceCpdir(self.ptr, s.as_ptr(), d.as_ptr(), err.as_mut_ptr(), 256)
        };
        if rc == KVSPACE_OK { Ok(()) } else { Err(err_str(&err)) }
    }

    pub fn mkindex(&self, path: &str, capacity: u32) -> Result<(), String> {
        let _ = capacity;
        let cp = CString::new(path).map_err(|e| e.to_string())?;
        let mut err = [0 as c_char; 256];
        let rc = unsafe { ffi::kvspaceMkindex(self.ptr, cp.as_ptr(), err.as_mut_ptr(), 256) };
        if rc == KVSPACE_OK { Ok(()) } else { Err(err_str(&err)) }
    }

    pub fn list(&self, prefix: &str) -> Vec<String> {
        let cp = match CString::new(prefix) {
            Ok(c) => c,
            Err(_) => return vec![],
        };
        let mut n: i64 = 0;
        let mut m: u32 = 0;
        let mut err = [0 as c_char; 256];
        let rc = unsafe {
            ffi::kvspaceListlen(self.ptr, cp.as_ptr(), &mut n, &mut m, err.as_mut_ptr(), 256)
        };
        if rc != KVSPACE_OK || n <= 0 {
            return vec![];
        }
        let cap = (n as usize).saturating_mul(m as usize + 1) as u32;
        if cap == 0 {
            return vec![];
        }
        let mut buf = vec![0u8; cap as usize];
        let (mut len, mut count, mut gen) = (0u32, 0u32, 0u64);
        let rc = unsafe {
            ffi::kvspaceList(
                self.ptr,
                cp.as_ptr(),
                buf.as_mut_ptr() as *mut c_char,
                cap,
                &mut len,
                &mut count,
                &mut gen,
                err.as_mut_ptr(),
                256,
            )
        };
        if rc != KVSPACE_OK {
            return vec![];
        }
        buf.truncate(len as usize);
        buf.split(|b| *b == 0)
            .filter(|s| !s.is_empty())
            .map(|s| String::from_utf8_lossy(s).into_owned())
            .collect()
    }
}

fn err_str(err: &[c_char; 256]) -> String {
    let bytes: Vec<u8> = err
        .iter()
        .take_while(|c| **c != 0)
        .map(|c| *c as u8)
        .collect();
    let s = String::from_utf8_lossy(&bytes).into_owned();
    if s.is_empty() { "kvspace: operation failed".into() } else { s }
}

impl Drop for KVSpace {
    fn drop(&mut self) {
        unsafe { ffi::kvspaceClose(self.ptr) };
        let _ = std::fs::remove_file(&self.path);
    }
}
