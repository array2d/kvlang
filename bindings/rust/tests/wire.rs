//! Wrapper <-> C constructor round-trip tests.
//!
//! 固定 16 字节 head：[bodylen:u64 LE@0][attrslen:u16 LE@8][padding:6@10-15]，body@16，
//! attr 紧随 body 且没有长度前缀，total = 16 + bodylen + attrslen。
//! record = [tag:u8][len:u16 LE][data]，tag 1 = langtype；tag 2（旧 reallen）已删除，出现即拒。
//! 构造走 C 的 kvlangXvalueHeadNew*，读侧用 xvalue::from_wire 本地解析。
use kvspace::xvalue;
use std::os::raw::c_char;
extern "C" {
    fn kvlangXvalueHeadNewScalar(
        raw: *const u8,
        raw_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> i32;
    fn kvlangXvalueHeadNewSlack(
        data: *const u8,
        data_len: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> i32;
    fn kvlangXvalueHeadNewPtr(
        path: *const u8,
        path_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> i32;
    fn kvlangXvalueHeadNewExt(
        locator: *const u8,
        locator_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> i32;
    fn free(p: *mut std::ffi::c_void);
}
/// C constructor oracle: (ref, body, langtype) -> full wire.
///
/// ref 只决定走哪个构造器（NewPtr/NewExt 会补 `*`/`@` 前缀）；head 里没有 ptr 位。
fn c_new(r#ref: u8, body: &[u8], langtype: &str) -> Vec<u8> {
    let lt = std::ffi::CString::new(langtype).unwrap();
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut total: u64 = 0;
    let bp = if body.is_empty() { std::ptr::null() } else { body.as_ptr() };
    let rc = match r#ref {
        xvalue::REF_PTR => unsafe {
            kvlangXvalueHeadNewPtr(bp, body.len() as u32, lt.as_ptr(), &mut out, &mut total)
        },
        xvalue::REF_EXT => unsafe {
            kvlangXvalueHeadNewExt(bp, body.len() as u32, lt.as_ptr(), &mut out, &mut total)
        },
        xvalue::REF_INLINE => unsafe {
            kvlangXvalueHeadNewSlack(bp, body.len() as u64, lt.as_ptr(), &mut out, &mut total)
        },
        _ => panic!("bad ref {ref}"),
    };
    assert_eq!(rc, 0, "c_new(ref={ref}) failed");
    let v = unsafe { std::slice::from_raw_parts(out, total as usize).to_vec() };
    unsafe { free(out as *mut std::ffi::c_void) };
    v
}
/// 独立 wire 解析器 -> (ref, body, langtype)，并校验 head/attr 自洽。
///
/// 只读 head 与 attr 区，不调用 xvalue::from_wire——两边各自独立实现。
fn c_peek(data: &[u8]) -> (u8, Vec<u8>, String) {
    assert!(data.len() >= xvalue::HEAD_LEN, "shorter than one head");
    let mut body_len: u64 = 0;
    for (i, b) in data[..8].iter().enumerate() {
        body_len |= (*b as u64) << (8 * i);
    }
    let attr_len = u16::from_le_bytes([data[8], data[9]]) as usize;
    assert!(
        data[10..10 + xvalue::PADDING_LEN].iter().all(|b| *b == 0),
        "padding must be zero"
    );
    let a = xvalue::HEAD_LEN + body_len as usize;
    assert_eq!(a + attr_len, data.len(), "total length mismatch");
    let attr = &data[a..a + attr_len];
    let mut p = 0usize;
    let mut lt = String::new();
    while p + 3 <= attr.len() {
        let tag = attr[p];
        let rlen = u16::from_le_bytes([attr[p + 1], attr[p + 2]]) as usize;
        assert_ne!(tag, xvalue::BANNED_ATTR_TAG_REALLEN, "tag 2 (old reallen) must not appear");
        if tag == xvalue::ATTR_TAG_LANGTYPE {
            lt = String::from_utf8(attr[p + 3..p + 3 + rlen].to_vec()).unwrap();
        }
        p += 3 + rlen;
    }
    let r#ref = match lt.as_bytes().first() {
        Some(b'*') => xvalue::REF_PTR,
        Some(b'@') => xvalue::REF_EXT,
        _ => xvalue::REF_INLINE,
    };
    (r#ref, data[xvalue::HEAD_LEN..a].to_vec(), lt)
}
struct Checker {
    failures: usize,
}
impl Checker {
    fn eq<T: std::fmt::Debug + PartialEq>(&mut self, name: &str, got: T, want: T) {
        if got == want {
            println!("  PASS {name}");
        } else {
            self.failures += 1;
            println!("  FAIL {name}: got {got:?}, want {want:?}");
        }
    }
}
#[test]
fn wrapper_reads_c_peeks() {
    let mut c = Checker { failures: 0 };
    let none = String::new();
    c.eq(
        "int64",
        c_peek(&xvalue::int64(42)),
        (xvalue::REF_INLINE, 42i64.to_le_bytes().to_vec(), none.clone()),
    );
    c.eq(
        "float64",
        c_peek(&xvalue::float64(1.5)),
        (xvalue::REF_INLINE, 1.5f64.to_le_bytes().to_vec(), none.clone()),
    );
    let s = "héllo"; // 5 chars, 6 bytes
    c.eq(
        "str",
        c_peek(&xvalue::string(s)),
        (xvalue::REF_INLINE, s.as_bytes().to_vec(), none.clone()),
    );
    c.eq(
        "index",
        c_peek(&xvalue::index()),
        (xvalue::REF_INLINE, Vec::new(), none.clone()),
    );
    // 引用性由构造器补的前缀表达：NewPtr -> `*`，NewExt -> 裸 `@`。
    c.eq(
        "link",
        c_peek(&xvalue::link("/t/x")),
        (xvalue::REF_PTR, b"/t/x".to_vec(), "*".to_string()),
    );
    c.eq(
        "ext",
        c_peek(&xvalue::ext("s3://b/k")),
        (xvalue::REF_EXT, b"s3://b/k".to_vec(), "@".to_string()),
    );
    assert_eq!(c.failures, 0, "{} failures", c.failures);
}
#[test]
fn c_new_wrapper_reads() {
    let mut c = Checker { failures: 0 };
    let raw = 7i64.to_le_bytes();
    let enc = c_new(xvalue::REF_INLINE, &raw, "int64");
    c.eq("int64", xvalue::from_wire(&enc), (xvalue::REF_INLINE, &raw[..]));
    let enc = c_new(xvalue::REF_INLINE, "héllo".as_bytes(), "char/utf8");
    c.eq(
        "str",
        xvalue::from_wire(&enc),
        (xvalue::REF_INLINE, "héllo".as_bytes()),
    );
    let enc = c_new(xvalue::REF_PTR, b"/t/x", "int64");
    c.eq("ptr", xvalue::from_wire(&enc), (xvalue::REF_PTR, &b"/t/x"[..]));
    let enc = c_new(xvalue::REF_EXT, b"s3://b/k", "");
    c.eq(
        "ext",
        xvalue::from_wire(&enc),
        (xvalue::REF_EXT, &b"s3://b/k"[..]),
    );
    // 畸形 / 缺 attr -> 空。
    c.eq(
        "garbage",
        xvalue::from_wire(&[0u8, 1, 2, 3]),
        (xvalue::REF_INLINE, &[][..]),
    );
    // 16 字节 head：bodylen=1、attrslen=0、padding 全零、body=42。
    let mut minimal = vec![1u8, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    minimal.extend_from_slice(&[0u8; 6]);
    minimal.push(42);
    c.eq(
        "minimal",
        xvalue::from_wire(&minimal),
        (xvalue::REF_INLINE, &[42u8][..]),
    );
    assert_eq!(c.failures, 0, "{} failures", c.failures);
}
#[test]
fn shm_roundtrip() {
    let dir = std::env::temp_dir().join(format!("kvspace-c-rs-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("db");
    let kv = kvspace::KVSpace::open(path.to_str().unwrap()).unwrap();
    kv.mkindex("/t/", 0);
    kv.set("/t/x", &xvalue::int64(42));
    let v = kv.get("/t/x").unwrap();
    assert_eq!(
        xvalue::from_wire(&v),
        (xvalue::REF_INLINE, &[42, 0, 0, 0, 0, 0, 0, 0][..])
    );
    assert_eq!(kv.list("/t/"), vec!["x".to_string()]);
    drop(kv);
    let _ = std::fs::remove_dir_all(&dir);
}
