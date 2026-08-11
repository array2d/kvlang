//! KVCpu — zero-copy FFI wrapper over kvspace-c.

use std::ffi::{c_char, c_int, c_void, CString};

#[link(name = "kvspace-c")]
extern "C" {
    fn kvspace_open(path: *const c_char, data_size: usize) -> *mut c_void;
    fn kvspace_close(kv: *mut c_void);
    fn kvspace_get(kv: *mut c_void, key: *const c_char, resolve: c_int, out_len: *mut i32) -> *mut u8;
    fn kvspace_set(kv: *mut c_void, key: *const c_char, val: *const u8, val_len: i32) -> c_int;
    fn kvspace_list(kv: *mut c_void, prefix: *const c_char, expand_ext: bool, resolve: c_int, names: *mut *mut *mut c_char, count: *mut i32) -> c_int;
    fn kvspace_mkindex(kv: *mut c_void, path: *const c_char) -> c_int;
    fn kvspace_del(kv: *mut c_void, key: *const c_char) -> c_int;
    fn kvspace_notify(kv: *mut c_void, key: *const c_char, val: *const u8, val_len: i32) -> c_int;
    fn kvspace_watch(kv: *mut c_void, key: *const c_char, timeout_ms: i32, out_len: *mut i32) -> *mut u8;
    fn kvspace_link(kv: *mut c_void, target: *const c_char, linkpath: *const c_char) -> c_int;
    fn kvspace_extindex(kv: *mut c_void, path: *const c_char, extpath: *const c_char) -> c_int;
}

pub trait Cpu {
    fn execute(&mut self, pc: &str) -> Result<(), String>;
    fn step(&mut self, pc: &str) -> Result<(), String>;
    fn debugger_active(&self) -> bool;
}

/// Raw XValue reference: points directly into SHM, no copy.
pub struct RawValue {
    pub kind: String,          // small, copied from TLV header
    pub body_len: i32,         // raw body length in bytes
    pub body_ptr: *const u8,   // direct pointer into SHM sbo_data; valid as long as kvspace_t lives
}
impl RawValue {
    pub unsafe fn bytes(&self) -> &[u8] { std::slice::from_raw_parts(self.body_ptr, self.body_len as usize) }
    pub unsafe fn i64(&self) -> i64 { (self.body_ptr as *const i64).read_unaligned() }
}

pub struct KVCpu {
    pub kv: *mut c_void,
    pub vm_id: String,
    last_tlv_ptr: *mut u8, // track last get() result for free-if-malloc
    last_tlv_len: i32,
}

impl KVCpu {
    pub fn open(shm_path: &str) -> Option<*mut c_void> {
        let cp = CString::new(shm_path).ok()?;
        let kv = unsafe { kvspace_open(cp.as_ptr(), 2097152) };
        if kv.is_null() { None } else { Some(kv) }
    }
    pub fn close(kv: *mut c_void) { unsafe { kvspace_close(kv); } }
    pub fn new(kv: *mut c_void, vm_id: &str) -> Self {
        KVCpu { kv, vm_id: vm_id.to_string(), last_tlv_ptr: std::ptr::null_mut(), last_tlv_len: 0 }
    }

    /// Zero-copy get: returns kind (copied, small) + body (direct SHM pointer).
    /// Body pointer is valid until next kvspace operation that modifies sbo_data.
    pub fn get(&self, key: &str) -> Option<RawValue> {
        let ck = CString::new(key).ok()?;
        let mut len: i32 = 0;
        let ptr = unsafe { kvspace_get(self.kv, ck.as_ptr(), 1, &mut len) };
        if ptr.is_null() || len == 0 { return None; }
        // ptr points into SHM sbo_data — do NOT free
        let data = unsafe { std::slice::from_raw_parts(ptr, len as usize) };
        if data.len() < 10 { return None; }
        let kl = data[0] as usize;
        let p = 1 + kl;
        if data.len() < p + 8 { return None; }
        let rl = i32::from_le_bytes([data[p+4],data[p+5],data[p+6],data[p+7]]);
        if (rl as usize) + p + 8 > data.len() { return None; }
        let kind = String::from_utf8_lossy(&data[1..p]).to_string();
        Some(RawValue {
            kind,
            body_len: rl,
            body_ptr: unsafe { ptr.add(p + 8) }, // pointer directly to body bytes
        })
    }

    /// is_malloc_ptr: check if a pointer came from malloc (not SHM).
    /// Currently all kvspace_get pointers are SHM-based (zero-copy).
    /// If ever we add a copy path, this would change.
    fn _is_shm_ptr(&self, _ptr: *const u8) -> bool { true }

    pub fn set(&self, key: &str, kind: &str, raw: &[u8]) {
        let ck = CString::new(key).unwrap();
        let mut tlv = vec![kind.len() as u8];
        tlv.extend_from_slice(kind.as_bytes());
        tlv.extend_from_slice(&1i32.to_le_bytes());
        tlv.extend_from_slice(&(raw.len() as i32).to_le_bytes());
        tlv.extend_from_slice(raw);
        unsafe { kvspace_set(self.kv, ck.as_ptr(), tlv.as_ptr(), tlv.len() as i32) };
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

/// Helper: read int64 from raw SHM body bytes (zero-copy).
pub unsafe fn body_i64(ptr: *const u8, len: i32) -> i64 {
    let n = (len as usize).min(8);
    let mut b = [0u8; 8];
    std::ptr::copy_nonoverlapping(ptr, b.as_mut_ptr(), n);
    i64::from_le_bytes(b)
}

/// Helper: read float64 from raw SHM body bytes (zero-copy).
pub unsafe fn body_f64(ptr: *const u8, len: i32) -> f64 {
    let n = (len as usize).min(8);
    let mut b = [0u8; 8];
    std::ptr::copy_nonoverlapping(ptr, b.as_mut_ptr(), n);
    f64::from_le_bytes(b)
}

/// Helper: read bytes from raw SHM body pointer (zero-copy).
pub unsafe fn body_bytes<'a>(ptr: *const u8, len: i32) -> &'a [u8] {
    std::slice::from_raw_parts(ptr, len as usize)
}
