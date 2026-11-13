
use std::ffi::CStr;
use std::fs;
use std::os::raw::{c_char, c_int};
use std::panic::catch_unwind;

use crate::{compile, format, init_dirs, kvkind, printlib, printstack, vet, Kv};

fn cstr<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(p).to_str().unwrap_or("") }
}

fn write_out(buf: *mut c_char, cap: u32, s: &str) {
    if buf.is_null() || cap == 0 {
        return;
    }
    let b = s.as_bytes();
    let n = b.len().min(cap as usize - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(b.as_ptr(), buf as *mut u8, n);
        *buf.add(n) = 0;
    }
}

fn layout_core(src: &str, dsn: &str) -> Result<String, String> {
    let mut kv = Kv::conn(dsn);
    init_dirs(&mut kv)?;
    Ok(compile(&mut kv, src)?.join("\n"))
}

fn finish(
    r: std::thread::Result<Result<String, String>>,
    entry_out: *mut c_char,
    entry_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    match r {
        Ok(Ok(entry)) => {
            write_out(entry_out, entry_cap, &entry);
            0
        }
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "layout panicked (invalid program)");
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn pathlangLayoutFile(
    path: *const c_char,
    dsn: *const c_char,
    entry_out: *mut c_char,
    entry_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let (path, dsn) = (cstr(path).to_string(), cstr(dsn).to_string());
    let r = catch_unwind(|| {
        let src = fs::read_to_string(&path).map_err(|e| format!("read {path}: {e}"))?;
        layout_core(&src, &dsn)
    });
    finish(r, entry_out, entry_cap, err_out, err_cap)
}

#[no_mangle]
pub extern "C" fn pathlangLayoutCode(
    src: *const c_char,
    dsn: *const c_char,
    entry_out: *mut c_char,
    entry_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let (src, dsn) = (cstr(src).to_string(), cstr(dsn).to_string());
    let r = catch_unwind(|| layout_core(&src, &dsn));
    finish(r, entry_out, entry_cap, err_out, err_cap)
}

#[no_mangle]
pub extern "C" fn pathlangLayoutVet(src: *const c_char, err_out: *mut c_char, err_cap: u32) -> i32 {
    let src = cstr(src).to_string();
    match catch_unwind(|| vet(&src)) {
        Ok(Ok(())) => 0,
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "vet panicked (invalid program)");
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn pathlangLayoutFormat(
    src: *const c_char,
    out: *mut c_char,
    out_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let src = cstr(src).to_string();
    match catch_unwind(|| format(&src)) {
        Ok(Ok(s)) => {
            write_out(out, out_cap, &s);
            0
        }
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "format panicked (invalid program)");
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn pathlangLayoutPrintlib(
    lib: *const c_char,
    dsn: *const c_char,
    out: *mut c_char,
    out_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let lib = cstr(lib).to_string();
    let dsn = cstr(dsn).to_string();
    let r = catch_unwind(|| -> Result<String, String> {
        let mut kv = Kv::conn(&dsn);
        Ok(printlib(&mut kv, &lib))
    });
    match r {
        Ok(Ok(s)) => {
            write_out(out, out_cap, &s);
            0
        }
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "printlib panicked");
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn pathlangLayoutPrintstack(
    vid: *const c_char,
    dsn: *const c_char,
    out: *mut c_char,
    out_cap: u32,
    err_out: *mut c_char,
    err_cap: u32,
) -> i32 {
    let vid = cstr(vid).to_string();
    let dsn = cstr(dsn).to_string();
    let r = catch_unwind(|| -> Result<String, String> {
        let mut kv = Kv::conn(&dsn);
        Ok(printstack(&mut kv, &vid))
    });
    match r {
        Ok(Ok(s)) => {
            write_out(out, out_cap, &s);
            0
        }
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "printstack panicked");
            -1
        }
    }
}

#[repr(C)]
pub struct pathlangKindexpr {
    pub ref_: i32,      
    pub ndim: i32,      
    pub dims: [i32; 8], 
    pub array_len: i32, 
    pub kind: [u8; 64], 
}

#[no_mangle]
pub unsafe extern "C" fn pathlangKindexprParse(
    langtype: *const c_char,
    out: *mut pathlangKindexpr,
) -> i32 {
    if langtype.is_null() || out.is_null() {
        return -1;
    }
    let s = cstr(langtype);
    match catch_unwind(|| parse_kindexpr(s)) {
        Ok(Some(v)) => {
            
            unsafe { out.write(v) };
            0
        }
        _ => -1,
    }
}

fn parse_kindexpr(s: &str) -> Option<pathlangKindexpr> {
    let (ref_, body) = match s.as_bytes().first()? {
        b'*' => (1, &s[1..]),
        b'@' => (2, &s[1..]),
        _ => (0, s),
    };
    if body.is_empty() {
        return None;
    }
    let (dims, kind) = kvkind::parse_langtype(body);
    let mut out = pathlangKindexpr {
        ref_,
        ndim: 0,
        dims: [0; 8],
        array_len: 1,
        kind: [0; 64],
    };
    if dims.len() > out.dims.len() {
        return None;
    }
    out.ndim = dims.len() as i32;
    for (i, &d) in dims.iter().enumerate() {
        if d < 0 {
            return None;
        }
        out.dims[i] = d;
        out.array_len = out.array_len.checked_mul(d)?;
    }
    let kb = kind.as_bytes();
    if kb.len() >= out.kind.len() {
        return None;
    }
    out.kind[..kb.len()].copy_from_slice(kb);
    Some(out)
}

#[no_mangle]
pub extern "C" fn pathlangLayoutDefineRwir(
    dsn: *const c_char,
    opcode: *const c_char,
    read_langtypes: *const *const c_char,
    read_count: u32,
    write_langtypes: *const *const c_char,
    write_count: u32,
    write_inplace: *const u8,
    variadic: c_int,
    err_out: *mut c_char,
    err_cap: u32,
) -> c_int {
    let dsn = cstr(dsn).to_string();
    let opcode = cstr(opcode).to_string();
    
    let (reads, writes) = match (
        collect_langtypes(read_langtypes, read_count),
        collect_langtypes(write_langtypes, write_count),
    ) {
        (Some(r), Some(w)) => (r, w),
        (None, _) => {
            write_out(
                err_out,
                err_cap,
                "define rwir: read_count > 0 but read_langtypes is NULL",
            );
            return -1;
        }
        (_, None) => {
            write_out(
                err_out,
                err_cap,
                "define rwir: write_count > 0 but write_langtypes is NULL",
            );
            return -1;
        }
    };
    let inplace: Vec<bool> = if write_inplace.is_null() {
        vec![false; write_count as usize]
    } else {
        (0..write_count as usize)
            .map(|i| unsafe { *write_inplace.add(i) } != 0)
            .collect()
    };

    let r = catch_unwind(|| define_rwir_core(&dsn, &opcode, reads, writes, inplace, variadic != 0));
    match r {
        Ok(Ok(())) => 0,
        Ok(Err(e)) => {
            write_out(err_out, err_cap, &e);
            -1
        }
        Err(_) => {
            write_out(err_out, err_cap, "define rwir panicked (invalid input)");
            -1
        }
    }
}

fn collect_langtypes(ptr: *const *const c_char, count: u32) -> Option<Vec<String>> {
    if count == 0 {
        return Some(Vec::new());
    }
    if ptr.is_null() {
        return None;
    }
    Some(
        (0..count as usize)
            .map(|i| cstr(unsafe { *ptr.add(i) }).to_string())
            .collect(),
    )
}

fn define_rwir_core(
    dsn: &str,
    opcode: &str,
    reads: Vec<String>,
    writes: Vec<String>,
    inplace: Vec<bool>,
    variadic: bool,
) -> Result<(), String> {
    if opcode.is_empty() {
        return Err("define rwir: opcode is empty".to_string());
    }
    if opcode.contains('/') {
        return Err(format!(
            "define rwir {opcode}: opcode must not contain '/'; use `·` for package separation"
        ));
    }
    
    if inplace.len() != writes.len() {
        return Err(format!(
            "define rwir {opcode}: internal write_inplace/write_count mismatch \
             ({} vs {})",
            inplace.len(),
            writes.len()
        ));
    }
    
    if variadic && reads.is_empty() {
        return Err(format!(
            "define rwir {opcode}: variadic requires at least one read parameter"
        ));
    }
    let mut params: Vec<crate::ast::Param> = reads.into_iter().map(|ty| param(ty, false)).collect();
    if variadic {
        let last = params.len() - 1;
        params[last].ty.push_str("...");
    }
    let returns: Vec<crate::ast::Param> = writes
        .into_iter()
        .zip(inplace)
        .map(|(ty, ip)| param(ty, ip))
        .collect();
    let decl = crate::ast::RwirDecl {
        comments: Vec::new(),
        sig: crate::ast::FuncSig {
            name: opcode.to_string(),
            params,
            returns,
        },
        pkg: String::new(), 
    };
    let mut kv = Kv::conn(dsn);
    init_dirs(&mut kv)?;
    crate::code::write_rwir_decl(&mut kv, &decl)
}

fn param(ty: String, inplace: bool) -> crate::ast::Param {
    crate::ast::Param {
        name: String::new(),
        ty,
        by_ref: String::new(),
        has_ty: true,
        inplace,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(s: &str) -> Option<pathlangKindexpr> {
        let c = std::ffi::CString::new(s).unwrap();
        let mut out = pathlangKindexpr {
            ref_: -9,
            ndim: -9,
            dims: [-9; 8],
            array_len: -9,
            kind: [0xff; 64],
        };
        let rc = unsafe { pathlangKindexprParse(c.as_ptr(), &mut out) };
        (rc == 0).then_some(out)
    }

    fn kind(k: &pathlangKindexpr) -> &str {
        let n = k.kind.iter().position(|&b| b == 0).unwrap();
        std::str::from_utf8(&k.kind[..n]).unwrap()
    }

    #[test]
    fn kindexpr_parse_contract() {
        let k = parse("[2,3]int64").unwrap();
        assert_eq!(
            (k.ref_, k.ndim, &k.dims[..2], k.array_len, kind(&k)),
            (0, 2, &[2, 3][..], 6, "int64")
        );
        let k = parse("*[4]float32").unwrap();
        assert_eq!(
            (k.ref_, k.ndim, k.array_len, kind(&k)),
            (1, 1, 4, "float32")
        );
        let k = parse("@char/utf8").unwrap();
        assert_eq!(
            (k.ref_, k.ndim, k.array_len, kind(&k)),
            (2, 0, 1, "char/utf8")
        );
        
        assert!(parse("[1,1,1,1,1,1,1,1,1]int64").is_none());
        assert!(parse("[65536,65536]int64").is_none());
        assert!(parse("[-1]int64").is_none());
        assert!(parse(&format!("/lib/{}", "x".repeat(70))).is_none());
        assert!(parse("").is_none() && parse("*").is_none());
        let rc = unsafe { pathlangKindexprParse(std::ptr::null(), std::ptr::null_mut()) };
        assert_eq!(rc, -1);
    }
}
