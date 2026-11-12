
use std::convert::TryInto;
use std::ffi::{c_char, c_int, c_void, CString};
use std::ptr;

const KVSPACE_XVALUE_HEAD_LEN: usize = 16;
const KVLANG_XVALUE_ATTR_TAG_LANGTYPE: u8 = 1;

fn xvalue_head_scan(wire: &[u8]) -> Vec<u8> {
    if wire.len() < KVSPACE_XVALUE_HEAD_LEN {
        return vec![];
    }
    let bodylen = u64::from_le_bytes(wire[0..8].try_into().unwrap()) as usize;
    let attrslen = u16::from_le_bytes(wire[8..10].try_into().unwrap()) as usize;
    if wire[10..16] != [0u8; 6] {
        return vec![];
    }
    if KVSPACE_XVALUE_HEAD_LEN + bodylen + attrslen > wire.len() {
        return vec![];
    }
    let mut p = KVSPACE_XVALUE_HEAD_LEN + bodylen;
    let mut left = attrslen;
    let mut langtype = vec![];
    while left >= 3 {
        let tag = wire[p];
        let rlen = u16::from_le_bytes([wire[p + 1], wire[p + 2]]) as usize;
        if left < 3 + rlen {
            return vec![];
        }
        match tag {
            KVLANG_XVALUE_ATTR_TAG_LANGTYPE => langtype = wire[p + 3..p + 3 + rlen].to_vec(),
            2 => return vec![], 
            _ => {}
        }
        p += 3 + rlen;
        left -= 3 + rlen;
    }
    langtype
}

fn xvalue_head_effective_len(wire: &[u8]) -> Option<u64> {
    if wire.len() < KVSPACE_XVALUE_HEAD_LEN {
        return None;
    }
    Some(u64::from_le_bytes(wire[0..8].try_into().unwrap()))
}
extern "C" {
    fn kvspaceConnect(dsn: *const c_char) -> *mut c_void;
    fn kvspaceClose(h: *mut c_void);
    
    fn kvspaceResolveOffset(h: *mut c_void, key: *const c_char, anchor: u64,
                      out_offset: *mut u64, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceRealloc(h: *mut c_void, key: *const c_char, body_len: u64,
                      attr_len: u64, out_offset: *mut u64, err: *mut c_char,
                      err_cap: u32) -> c_int;
    fn kvspaceFree(h: *mut c_void, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceDeltree(h: *mut c_void, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceMkindex(h: *mut c_void, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceListlen(h: *mut c_void, key: *const c_char, out_count: *mut i64,
                      out_max_name: *mut u32, err: *mut c_char, err_cap: u32) -> c_int;
    fn kvspaceList(h: *mut c_void, key: *const c_char, out_names: *mut c_char,
                   out_cap: u32, out_len: *mut u32, out_count: *mut u32,
                   out_gen: *mut u64, err: *mut c_char, err_cap: u32) -> c_int;
    
    fn kvspaceXvalueInProcessAddress(h: *mut c_void) -> *mut u8;
}

fn cs(s: &str) -> CString { CString::new(s).unwrap() }

struct KV { ptr: *mut c_void, path: String }

impl KV {
    fn open(path: &str) -> Self {
        let _ = std::fs::remove_file(path);
        let dsn = CString::new(format!("shm://{}", path)).unwrap();
        let ptr = unsafe { kvspaceConnect(dsn.as_ptr()) };
        assert!(!ptr.is_null());
        KV { ptr, path: path.to_string() }
    }
    
    fn base(&self) -> *mut u8 {
        let b = unsafe { kvspaceXvalueInProcessAddress(self.ptr) };
        assert!(!b.is_null(), "no in-process address space (shm backend expected)");
        b
    }

    fn set(&self, key: &str, langtype: &str, body: &[u8]) {
        let lt = langtype.as_bytes();
        let attr_len = if lt.is_empty() { 0 } else { 3 + lt.len() as u64 };
        let ck = cs(key);
        let mut off: u64 = 0;
        let mut err = [0 as c_char; 256];
        let rc = unsafe {
            kvspaceRealloc(self.ptr, ck.as_ptr(), body.len() as u64, attr_len,
                           &mut off, err.as_mut_ptr(), 256)
        };
        assert_eq!(rc, 0, "realloc {key} failed");
        let base = self.base();
        unsafe {
            if !body.is_empty() {
                ptr::copy_nonoverlapping(body.as_ptr(),
                                         base.add(off as usize + KVSPACE_XVALUE_HEAD_LEN),
                                         body.len());
            }
            if !lt.is_empty() {
                let a = base.add(off as usize + KVSPACE_XVALUE_HEAD_LEN + body.len());
                let hdr = [KVLANG_XVALUE_ATTR_TAG_LANGTYPE, lt.len() as u8,
                           (lt.len() >> 8) as u8];
                ptr::copy_nonoverlapping(hdr.as_ptr(), a, 3);
                ptr::copy_nonoverlapping(lt.as_ptr(), a.add(3), lt.len());
            }
        }
    }

    fn get(&self, key: &str) -> Option<(String, Vec<u8>)> {
        let ck = cs(key);
        let mut off: u64 = 0;
        let mut err = [0 as c_char; 256];
        let rc = unsafe {
            kvspaceResolveOffset(self.ptr, ck.as_ptr(), 0, &mut off, err.as_mut_ptr(), 256)
        };
        if rc != 0 {
            return None;
        }
        let base = self.base();
        
        let head = unsafe { std::slice::from_raw_parts(base.add(off as usize), KVSPACE_XVALUE_HEAD_LEN) };
        let body_len = u64::from_le_bytes(head[0..8].try_into().unwrap()) as usize;
        let attrs_len = u16::from_le_bytes([head[8], head[9]]) as usize;
        let total = KVSPACE_XVALUE_HEAD_LEN + body_len + attrs_len;
        let wire = unsafe { std::slice::from_raw_parts(base.add(off as usize), total) };
        let mut kind = String::from_utf8_lossy(&xvalue_head_scan(wire)).into_owned();
        if kind.starts_with('*') || kind.starts_with('@') {
            kind.drain(..1);
        }
        if let Some(i) = kind.find(']') {
            kind.drain(..=i);
        }
        Some((kind, wire[KVSPACE_XVALUE_HEAD_LEN..KVSPACE_XVALUE_HEAD_LEN + body_len].to_vec()))
    }

    fn mkindex(&self, path: &str) {
        let cp = cs(path);
        let mut err = [0 as c_char; 256];
        unsafe { kvspaceMkindex(self.ptr, cp.as_ptr(), err.as_mut_ptr(), 256); }
    }
    fn del(&self, key: &str) {
        let ck = cs(key);
        let mut err = [0 as c_char; 256];
        unsafe { kvspaceFree(self.ptr, ck.as_ptr(), err.as_mut_ptr(), 256); }
    }
    fn deltree(&self, prefix: &str) {
        let cp = cs(prefix);
        let mut err = [0 as c_char; 256];
        unsafe { kvspaceDeltree(self.ptr, cp.as_ptr(), err.as_mut_ptr(), 256); }
    }
    
    fn list(&self, prefix: &str) -> Vec<String> {
        let cp = cs(prefix);
        let mut count: i64 = 0;
        let mut max_name: u32 = 0;
        let mut err = [0 as c_char; 256];
        let rc = unsafe {
            kvspaceListlen(self.ptr, cp.as_ptr(), &mut count, &mut max_name,
                           err.as_mut_ptr(), 256)
        };
        if rc != 0 || count <= 0 {
            return vec![];
        }
        let cap = (count as usize) * (max_name as usize + 1);
        let mut buf = vec![0u8; cap];
        let mut length: u32 = 0;
        let mut listed: u32 = 0;
        let mut gen: u64 = 0;
        let rc = unsafe {
            kvspaceList(self.ptr, cp.as_ptr(), buf.as_mut_ptr() as *mut c_char,
                        cap as u32, &mut length, &mut listed, &mut gen,
                        err.as_mut_ptr(), 256)
        };
        if rc != 0 {
            return vec![];
        }
        buf[..length as usize]
            .split(|&b| b == 0)
            .filter(|s| !s.is_empty())
            .map(|s| String::from_utf8_lossy(s).into_owned())
            .collect()
    }
}

impl Drop for KV {
    fn drop(&mut self) {
        unsafe { kvspaceClose(self.ptr); }
        let _ = std::fs::remove_file(&self.path);
    }
}

fn main() {
    let kv = KV::open("/tmp/kvspace_rs.shm");
    kv.mkindex("/rs/");
    kv.set("/rs/a", "int64", &42i64.to_le_bytes());
    kv.set("/rs/b", "char/utf8", b"hello");

    let (kind, raw) = kv.get("/rs/a").unwrap();
    let val = i64::from_le_bytes(raw[..8].try_into().unwrap());
    println!("/rs/a  kind={kind}  val={val}");
    let (kind, raw) = kv.get("/rs/b").unwrap();
    println!("/rs/b  kind={kind}  val={}", String::from_utf8_lossy(&raw));

    let ns = kv.list("/rs/");
    println!("list /rs/: {ns:?} (count={})", ns.len());

    kv.deltree("/rs/");
    println!("PASS rust");
}
