
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};

pub type Handle = *mut c_void;

extern "C" {
    fn pathspaceConnect(dsn: *const c_char) -> Handle;
    fn pathspaceClose(h: Handle);
    
    fn free(p: *mut c_void);

    fn pathspaceResolveOffset(
        h: Handle,
        key: *const c_char,
        parent_key_anchor: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    fn pathspaceRealloc(
        h: Handle,
        key: *const c_char,
        body_length: u64,
        attr_length: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    fn pathspaceFree(h: Handle, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    
    fn pathspaceXvalueInProcessAddress(h: Handle) -> *mut u8;
    
    fn pathspaceListlen(
        h: Handle,
        key: *const c_char,
        out_count: *mut i64,
        out_max_name_length: *mut u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    fn pathspaceList(
        h: Handle,
        key: *const c_char,
        out_names: *mut c_char,
        out_capacity: u32,
        out_length: *mut u32,
        out_count: *mut u32,
        out_generation: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn pathspaceDeltree(h: Handle, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn pathspaceMkindex(h: Handle, path: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    
    fn pathlangXvalueHeadNewNone(out: *mut *mut u8, out_len: *mut u64) -> c_int;
    
    fn pathlangXvalueHeadNewScalar(
        raw: *const u8,
        raw_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    fn pathlangXvalueHeadNewSlack(
        data: *const u8,
        data_len: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    fn pathlangXvalueHeadNewTensor(
        dims: *const u64,
        ndim: u32,
        raw: *const u8,
        raw_len: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    fn pathlangXvalueHeadNewCode(
        body: *const u8,
        body_len: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;

    fn pathlangXvalueNewChar(
        bytes: *const u8,
        len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn pathlangXvalueNewBool(
        v: u8,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn pathlangXvalueNewInt64(
        v: i64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
    fn pathlangXvalueNewFloat64(
        v: f64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u32,
    ) -> c_int;
}

pub const PREFIX: usize = 16;

const OFF_ATTRSLEN: usize = 8;

const OFF_PADDING: usize = 10;

#[inline]
pub fn xh_len(data: &[u8]) -> u64 {
    match data.get(0..8) {
        Some(b) => u64::from_le_bytes([b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]]),
        None => 0,
    }
}

#[inline]
pub fn xh_attrs_len(data: &[u8]) -> u16 {
    match data.get(OFF_ATTRSLEN..OFF_ATTRSLEN + 2) {
        Some(b) => u16::from_le_bytes([b[0], b[1]]),
        None => 0,
    }
}

#[inline]
pub fn xh_padding(data: &[u8]) -> Option<&[u8]> {
    data.get(OFF_PADDING..PREFIX)
}

#[inline]
pub fn langtype_is_pointer(langtype: &str) -> bool {
    langtype.starts_with('*')
}

#[inline]
pub fn xh_is_none(data: &[u8]) -> bool {
    data.is_empty() || (data.len() >= PREFIX && xh_len(data) == 0)
}

pub fn xh_body_slice(data: &[u8]) -> &[u8] {
    let len = xh_len(data) as usize;
    let Some(end) = PREFIX.checked_add(len) else {
        return &[];
    };
    if end > data.len() {
        return &[];
    }
    &data[PREFIX..end]
}

fn err_ret(buf: &mut [c_char; 256], ret: c_int) -> Result<(), String> {
    if ret == 0 {
        return Ok(());
    }
    let msg = unsafe { CStr::from_ptr(buf.as_ptr()) }
        .to_string_lossy()
        .into_owned();
    Err(if msg.is_empty() {
        "pathspace: error".to_string()
    } else {
        msg
    })
}

fn call_codec(
    what: &str,
    f: impl FnOnce(*mut *mut u8, *mut u32) -> c_int,
) -> Result<Vec<u8>, String> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u32 = 0;
    let rc = f(&mut out, &mut out_len);
    if rc != 0 || out.is_null() || out_len == 0 {
        if !out.is_null() {
            unsafe { free(out as *mut c_void) };
        }
        return Err(format!(
            "pathspace codec {what} failed (rc={rc}, len={out_len})"
        ));
    }
    let bytes = unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec();
    unsafe { free(out as *mut c_void) };
    Ok(bytes)
}

fn call_codec64(
    what: &str,
    f: impl FnOnce(*mut *mut u8, *mut u64) -> c_int,
) -> Result<Vec<u8>, String> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u64 = 0;
    let rc = f(&mut out, &mut out_len);
    if rc != 0 || out.is_null() || out_len == 0 || out_len > u32::MAX as u64 {
        if !out.is_null() {
            unsafe { free(out as *mut c_void) };
        }
        return Err(format!(
            "pathspace codec {what} failed (rc={rc}, len={out_len})"
        ));
    }
    let bytes = unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec();
    unsafe { free(out as *mut c_void) };
    Ok(bytes)
}

fn connect_or_panic(dsn: &str) -> Handle {
    let c = CString::new(dsn).expect("no NUL in dsn");
    let h = unsafe { pathspaceConnect(c.as_ptr()) };
    if h.is_null() {
        panic!("pathspace connect failed: {dsn}");
    }
    h
}

pub fn test_dsn(tag: &str) -> String {
    format!("shm:///tmp/pathlanglayout_{}_{}", tag, std::process::id())
}

pub fn remove_test_store(dsn: &str) {
    let path = dsn.trim_start_matches("shm://");
    for suffix in ["", ".garb", ".gen", ".sbo.data", ".sbo.head"] {
        let _ = std::fs::remove_file(format!("{path}{suffix}"));
    }
}

pub fn try_connect(dsn: &str) -> Option<Handle> {
    let c = CString::new(dsn).ok()?;
    let h = unsafe { pathspaceConnect(c.as_ptr()) };
    (!h.is_null()).then_some(h)
}

pub struct Kv {
    h: Handle,
}

impl Kv {
    pub fn conn(dsn: &str) -> Kv {
        Kv {
            h: connect_or_panic(dsn),
        }
    }

    pub fn try_conn(dsn: &str) -> Result<Kv, String> {
        let c = CString::new(dsn).map_err(|_| format!("dsn contains NUL: {dsn:?}"))?;
        let h = unsafe { pathspaceConnect(c.as_ptr()) };
        if h.is_null() {
            return Err(format!("pathspace connect failed: {dsn}"));
        }
        Ok(Kv { h })
    }

    pub fn set(&mut self, pairs: &[(String, String, Vec<u8>)]) -> Result<(), String> {
        for (key, lt, xvalue) in pairs {
            
            let last_seg = key.rsplit('/').next().unwrap_or(key);
            
            let wire = if xvalue.is_empty() || lt.is_empty() || last_seg.contains('\u{2025}') {
                xvalue.clone()
            } else {
                with_langtype(xvalue, lt)
            };
            self.set_one(key, &wire)?;
        }
        Ok(())
    }

    fn set_one(&mut self, key: &str, xvalue: &[u8]) -> Result<(), String> {
        let ck = CString::new(key).expect("no NUL in key");
        let mut err: [c_char; 256] = [0; 256];

        if xvalue.is_empty() {
            let ret =
                unsafe { pathspaceFree(self.h, ck.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
            return err_ret(&mut err, ret);
        }
        if xvalue.len() < 16 {
            return Err(format!("{key}: wire shorter than 16-byte head"));
        }
        
        let body_len = u64::from_le_bytes(xvalue[0..8].try_into().unwrap());
        let attr_len = u16::from_le_bytes([xvalue[8], xvalue[9]]) as u64;
        let want = 16u64
            .checked_add(body_len)
            .and_then(|v| v.checked_add(attr_len))
            .ok_or_else(|| format!("{key}: wire length overflow"))?;
        if (xvalue.len() as u64) < want {
            return Err(format!(
                "{key}: wire truncated (head says {want} bytes, got {})",
                xvalue.len()
            ));
        }

        let mut off: u64 = 0;
        let ret = unsafe {
            pathspaceRealloc(
                self.h,
                ck.as_ptr(),
                body_len,
                attr_len,
                &mut off,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        if ret != 0 {
            return err_ret(&mut err, ret);
        }
        let base = unsafe { pathspaceXvalueInProcessAddress(self.h) };
        if base.is_null() {
            return Err(format!(
                "{key}: backend has no in-process address space (durable cannot be written \
                 through the new ABI); write via a mapped backend"
            ));
        }
        unsafe {
            let dst = base.add(off as usize + 16);
            if body_len > 0 {
                std::ptr::copy_nonoverlapping(xvalue[16..].as_ptr(), dst, body_len as usize);
            }
            if attr_len > 0 {
                std::ptr::copy_nonoverlapping(
                    xvalue[16 + body_len as usize..].as_ptr(),
                    dst.add(body_len as usize),
                    attr_len as usize,
                );
            }
        }
        Ok(())
    }

    pub fn get_one(&mut self, key: &str) -> Vec<u8> {
        let c = CString::new(key).expect("no NUL in key");
        let mut off: u64 = 0;
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            pathspaceResolveOffset(
                self.h,
                c.as_ptr(),
                0,
                &mut off,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        if ret != 0 {
            return Vec::new();
        }
        let base = unsafe { pathspaceXvalueInProcessAddress(self.h) };
        if base.is_null() {
            return Vec::new();
        }
        unsafe {
            
            let head = std::slice::from_raw_parts(base.add(off as usize), 16);
            let body_len = u64::from_le_bytes(head[0..8].try_into().unwrap()) as usize;
            let attrs_len = u16::from_le_bytes([head[8], head[9]]) as usize;
            let total = 16 + body_len + attrs_len;
            std::slice::from_raw_parts(base.add(off as usize), total).to_vec()
        }
    }

    pub fn langtype_of(&mut self, key: &str) -> String {
        let wire = self.get_one(key);
        wire_langtype(&wire).unwrap_or_default()
    }

    pub fn list(&mut self, prefix: &str, _expand_ext: bool, _resolve: bool) -> Vec<String> {
        let c = CString::new(prefix).expect("no NUL in prefix");
        let mut count: i64 = 0;
        let mut max_name: u32 = 0;
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe {
            pathspaceListlen(
                self.h,
                c.as_ptr(),
                &mut count,
                &mut max_name,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        if ret != 0 || count <= 0 {
            return Vec::new();
        }
        
        let cap = (count as usize).saturating_mul(max_name as usize + 1);
        if cap == 0 {
            return Vec::new();
        }
        let mut buf = vec![0u8; cap];
        let mut length: u32 = 0;
        let mut listed: u32 = 0;
        let mut generation: u64 = 0;
        let ret = unsafe {
            pathspaceList(
                self.h,
                c.as_ptr(),
                buf.as_mut_ptr() as *mut c_char,
                cap as u32,
                &mut length,
                &mut listed,
                &mut generation,
                err.as_mut_ptr(),
                err.len() as u32,
            )
        };
        if ret != 0 {
            return Vec::new();
        }
        buf[..length as usize]
            .split(|&b| b == 0)
            .filter(|s| !s.is_empty())
            .map(|s| String::from_utf8_lossy(s).into_owned())
            .collect()
    }

    pub fn del_tree(&mut self, prefix: &str) -> Result<(), String> {
        let c = CString::new(prefix).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe { pathspaceDeltree(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }

    pub fn mkindex(&mut self, path: &str) -> Result<(), String> {
        let c = CString::new(path).expect("no NUL");
        let mut err: [c_char; 256] = [0; 256];
        let ret = unsafe { pathspaceMkindex(self.h, c.as_ptr(), err.as_mut_ptr(), err.len() as u32) };
        err_ret(&mut err, ret)
    }
}

impl Drop for Kv {
    fn drop(&mut self) {
        unsafe { pathspaceClose(self.h) };
    }
}

fn al_to_dims(kind: &str, array_len: i32) -> Vec<i32> {
    if kind.starts_with("char/") {
        vec![array_len.max(0)]
    } else if array_len > 1 {
        vec![array_len]
    } else {
        Vec::new()
    }
}

pub static EMPTY_CSTR: [u8; 1] = [0];

pub fn langtype_cstr(langtype: &str) -> CString {
    CString::new(langtype).unwrap_or_else(|_| CString::new("").unwrap())
}

pub fn with_langtype(wire: &[u8], langtype: &str) -> Vec<u8> {
    if wire.len() < PREFIX {
        return wire.to_vec();
    }
    let bodylen = xh_len(wire) as usize;
    let Some(body_end) = PREFIX.checked_add(bodylen) else {
        return wire.to_vec();
    };
    if body_end > wire.len() {
        return wire.to_vec();
    }
    let mut out = wire[..body_end].to_vec();
    let attr_len_pos = OFF_ATTRSLEN;
    out[OFF_PADDING..PREFIX].fill(0); 
    if langtype.is_empty() {
        out[attr_len_pos..attr_len_pos + 2].copy_from_slice(&0u16.to_le_bytes());
        return out;
    }
    let l = langtype.len();
    if l > usize::from(u16::MAX) - 3 {
        return wire.to_vec();
    }
    let mut attr = Vec::with_capacity(3 + l);
    attr.push(1u8); 
    attr.extend_from_slice(&(l as u16).to_le_bytes());
    attr.extend_from_slice(langtype.as_bytes());
    let alen = u16::try_from(attr.len()).expect("attr region fits u16");
    out[attr_len_pos..attr_len_pos + 2].copy_from_slice(&alen.to_le_bytes());
    out.extend_from_slice(&attr);
    out
}

pub fn xvalue_to_wire(kind: &str, raw: &[u8], array_len: i32) -> Result<Vec<u8>, String> {
    let dims = al_to_dims(kind, array_len);
    if (kind.is_empty() || kind == "None") && raw.is_empty() {
        return call_codec64("HeadNewNone", |out, out_len| unsafe {
            pathlangXvalueHeadNewNone(out, out_len)
        });
    }
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    if kind == "char" || kind.starts_with("char/") {
        return call_codec64("HeadNewSlack", |out, out_len| unsafe {
            pathlangXvalueHeadNewSlack(raw.as_ptr(), raw.len() as u64, empty, out, out_len)
        });
    }
    if !dims.is_empty() && !kind.contains('·') && !kind.starts_with('/') {
        let d: Vec<u64> = dims.iter().map(|&x| x as u64).collect();
        return call_codec64("HeadNewTensor", |out, out_len| unsafe {
            pathlangXvalueHeadNewTensor(
                d.as_ptr(),
                d.len() as u32,
                raw.as_ptr(),
                raw.len() as u64,
                empty,
                out,
                out_len,
            )
        });
    }
    if kind == "rwir" || kind == "def langtype" || (kind == "rwfunc" && !raw.is_empty()) {
        return call_codec64("HeadNewCode", |out, out_len| unsafe {
            pathlangXvalueHeadNewCode(raw.as_ptr(), raw.len() as u64, empty, out, out_len)
        });
    }
    call_codec64("HeadNewScalar", |out, out_len| unsafe {
        pathlangXvalueHeadNewScalar(raw.as_ptr(), raw.len() as u32, empty, out, out_len)
    })
}

#[inline]
fn xh_padding_is_zero(data: &[u8]) -> bool {
    xh_padding(data).is_some_and(|p| p.iter().all(|&byte| byte == 0))
}

pub fn wire_langtype(wire: &[u8]) -> Option<String> {
    if wire.len() < PREFIX || !xh_padding_is_zero(wire) {
        return None;
    }
    let body_len = xh_len(wire) as usize;
    let attrs_len = xh_attrs_len(wire) as usize;
    let start = PREFIX.checked_add(body_len)?;
    let end = start.checked_add(attrs_len)?;
    if end > wire.len() {
        return None;
    }
    let mut at = start;
    while at + 3 <= end {
        let tag = wire[at];
        let rlen = u16::from_le_bytes([wire[at + 1], wire[at + 2]]) as usize;
        let data_start = at + 3;
        let data_end = data_start.checked_add(rlen)?;
        if data_end > end {
            return None;
        }
        if tag == 1 {
            return Some(String::from_utf8_lossy(&wire[data_start..data_end]).into_owned());
        }
        at = data_end;
    }
    None
}

pub fn new_char(kind: &str, s: &str) -> Result<Vec<u8>, String> {
    let bytes = s.as_bytes();
    if kind == "char/utf8" {
        return new_char_byte(bytes);
    }
    let raw = if kind == "char/utf32" {
        s.chars().flat_map(|c| (c as u32).to_le_bytes()).collect()
    } else {
        bytes.to_vec()
    };
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec64("HeadNewSlack", |out, out_len| unsafe {
        pathlangXvalueHeadNewSlack(raw.as_ptr(), raw.len() as u64, empty, out, out_len)
    })
}

pub fn new_char_byte(bytes: &[u8]) -> Result<Vec<u8>, String> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec("NewChar", |out, out_len| unsafe {
        pathlangXvalueNewChar(bytes.as_ptr(), bytes.len() as u32, empty, out, out_len)
    })
}

pub fn new_none() -> Result<Vec<u8>, String> {
    call_codec64("HeadNewNone", |out, out_len| unsafe {
        pathlangXvalueHeadNewNone(out, out_len)
    })
}

pub fn new_bool(v: bool) -> Result<Vec<u8>, String> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec("NewBool", |out, out_len| unsafe {
        pathlangXvalueNewBool(v as u8, empty, out, out_len)
    })
}

pub fn new_int64(v: i64) -> Result<Vec<u8>, String> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec("NewInt64", |out, out_len| unsafe {
        pathlangXvalueNewInt64(v, empty, out, out_len)
    })
}

pub fn new_float64(v: f64) -> Result<Vec<u8>, String> {
    let empty = EMPTY_CSTR.as_ptr() as *const c_char;
    call_codec("NewFloat64", |out, out_len| unsafe {
        pathlangXvalueNewFloat64(v, empty, out, out_len)
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn codec_failure_is_error_not_empty_value() {
        assert!(call_codec("t", |_, _| -1).is_err());
        assert!(call_codec64("t", |_, _| -1).is_err());
        
        assert!(call_codec("t", |_, _| 0).is_err());
        assert!(call_codec64("t", |_, _| 0).is_err());
        let v = new_int64(7).unwrap();
        assert_eq!(xh_body_slice(&v), 7i64.to_le_bytes());
    }
}
