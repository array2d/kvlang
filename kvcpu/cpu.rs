//! KVCpu — zero-copy FFI wrapper over kvspace-c.

use std::ffi::{c_char, c_int, c_void, CString};

#[link(name = "kvspace-c")]
extern "C" {
    fn kvspace_open(path: *const c_char, data_size: usize) -> *mut c_void;
    fn kvspace_close(kv: *mut c_void);
    fn kvspace_get(kv: *mut c_void, key: *const c_char, resolve: c_int, out_len: *mut i32) -> *mut u8;
    fn kvspace_set(kv: *mut c_void, key: *const c_char, val: *const u8, val_len: i32) -> c_int;
    fn kvspace_list(kv: *mut c_void, prefix: *const c_char, expand_ext: c_int, resolve: c_int, names: *mut *mut *mut c_char, count: *mut i32) -> c_int;
    fn kvspace_mkindex(kv: *mut c_void, path: *const c_char) -> c_int;
    fn kvspace_del(kv: *mut c_void, key: *const c_char) -> c_int;
    fn kvspace_notify(kv: *mut c_void, key: *const c_char, val: *const u8, val_len: i32) -> c_int;
    fn kvspace_watch(kv: *mut c_void, key: *const c_char, timeout_ms: i32, out_len: *mut i32) -> *mut u8;
    fn kvspace_link(kv: *mut c_void, target: *const c_char, linkpath: *const c_char) -> c_int;
}

/// Raw XValue reference: points directly into SHM, zero-copy.
/// TLV header: [1B: b7=isptr|b6-0=kind_len][N B kind][4B al LE][4B raw_len LE][M B raw_body]
pub struct RawValue {
    pub is_ptr: bool,           // bit7 of header byte
    pub kind: String,           // small, copied from TLV header
    pub array_len: i32,
    pub body_len: i32,          // raw body length
    pub body_ptr: *const u8,   // direct pointer into SHM; zero-copy
}

impl RawValue {
    pub unsafe fn bytes(&self) -> &[u8] { std::slice::from_raw_parts(self.body_ptr, self.body_len as usize) }
    pub unsafe fn i64(&self) -> i64 { (self.body_ptr as *const i64).read_unaligned() }
    pub unsafe fn f64(&self) -> f64 { (self.body_ptr as *const f64).read_unaligned() }
    pub unsafe fn u16_le(&self, offset: usize) -> u16 {
        let ptr = self.body_ptr.add(offset);
        (ptr as *const u16).read_unaligned()
    }
    /// body as &str (zero-copy)
    pub unsafe fn body_str(&self) -> &str {
        let s = std::slice::from_raw_parts(self.body_ptr, self.body_len as usize);
        std::str::from_utf8_unchecked(s)
    }
    /// ptr target path: body is the target key
    pub unsafe fn ptr_target(&self) -> &str { self.body_str() }
}

pub struct KVCpu {
    pub kv: *mut c_void,
    pub vm_id: String,
}

impl KVCpu {
    pub fn open(shm_path: &str) -> Option<*mut c_void> {
        let cp = CString::new(shm_path).ok()?;
        let kv = unsafe { kvspace_open(cp.as_ptr(), 2097152) };
        if kv.is_null() { None } else { Some(kv) }
    }

    pub fn close(kv: *mut c_void) { unsafe { kvspace_close(kv); } }

    pub fn new(kv: *mut c_void, vm_id: &str) -> Self {
        KVCpu { kv, vm_id: vm_id.to_string() }
    }

    /// Zero-copy get: parses TLV, returns RawValue with SHM body pointer.
    /// is_ptr is bit7 of first header byte.
    pub fn get(&self, key: &str) -> Option<RawValue> {
        let ck = CString::new(key).ok()?;
        let mut len: i32 = 0;
        let ptr = unsafe { kvspace_get(self.kv, ck.as_ptr(), 1, &mut len) };
        if ptr.is_null() || len == 0 { return None; }
        let data = unsafe { std::slice::from_raw_parts(ptr, len as usize) };
        if data.len() < 10 { return None; }

        let is_ptr = data[0] & 0x80 != 0;
        let kl = (data[0] & 0x7F) as usize;
        let p = 1 + kl;
        if data.len() < p + 8 { return None; }

        let kind = String::from_utf8_lossy(&data[1..p]).to_string();
        let al = i32::from_le_bytes([data[p], data[p+1], data[p+2], data[p+3]]);
        let rl = i32::from_le_bytes([data[p+4], data[p+5], data[p+6], data[p+7]]);
        if (rl as usize) + p + 8 > data.len() { return None; }

        Some(RawValue {
            is_ptr,
            kind,
            array_len: al,
            body_len: rl,
            body_ptr: unsafe { ptr.add(p + 8) },
        })
    }

    pub fn set(&self, key: &str, kind: &str, raw: &[u8], al: i32, is_ptr: bool) {
        let ck = CString::new(key).unwrap();
        let first_byte = if is_ptr { kind.len() as u8 | 0x80 } else { kind.len() as u8 };
        let mut tlv = vec![first_byte];
        tlv.extend_from_slice(kind.as_bytes());
        tlv.extend_from_slice(&al.to_le_bytes());
        tlv.extend_from_slice(&(raw.len() as i32).to_le_bytes());
        tlv.extend_from_slice(raw);
        unsafe { kvspace_set(self.kv, ck.as_ptr(), tlv.as_ptr(), tlv.len() as i32) };
    }

    /// List children of a directory prefix, filtering by prefix.
    pub fn list(&self, prefix: &str) -> Vec<String> {
        let cp = CString::new(prefix).ok();
        if cp.is_none() { return vec![]; }
        let mut names: *mut *mut c_char = std::ptr::null_mut();
        let mut count: i32 = 0;
        let rc = unsafe { kvspace_list(self.kv, cp.unwrap().as_ptr(), 0, 0, &mut names, &mut count) };
        if rc != 0 || count <= 0 || names.is_null() { return vec![]; }
        let mut result = Vec::with_capacity(count as usize);
        for i in 0..count as isize {
            let s = unsafe { std::ffi::CStr::from_ptr(*names.offset(i)) };
            if let Ok(s) = s.to_str() { result.push(s.to_string()); }
        }
        result
    }

    pub fn mkindex(&self, path: &str) {
        let cp = CString::new(path).unwrap();
        unsafe { kvspace_mkindex(self.kv, cp.as_ptr()) };
    }

    pub fn del(&self, key: &str) {
        if let Ok(ck) = CString::new(key) {
            unsafe { kvspace_del(self.kv, ck.as_ptr()) };
        }
    }
}
