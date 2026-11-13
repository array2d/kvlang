
use std::ffi::{c_char, c_int, c_void, CStr, CString};

pub const HEAD_LEN: usize = 16;

pub const PATHSPACE_ATTR_TAG_LANGTYPE: u8 = 1;

pub fn value_langtype(wire: &[u8]) -> String {
    let mut lt: *const u8 = std::ptr::null();
    let mut len: u16 = 0;
    
    let rc = unsafe {
        pathspaceXvalueGetAttr(wire.as_ptr(), wire.len() as u64,
                             PATHSPACE_ATTR_TAG_LANGTYPE, &mut lt, &mut len)
    };
    if rc != 0 || lt.is_null() || len == 0 {
        return String::new();
    }
    String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(lt, len as usize) }).into_owned()
}

pub fn value_effective_len(wire: &[u8]) -> Option<usize> {
    let mut out: u64 = 0;
    let rc = unsafe { pathspaceXvalueHeadEffectiveLen(wire.as_ptr(), wire.len() as u64, &mut out) };
    if rc != 0 || out > usize::MAX as u64 {
        return None;
    }
    Some(out as usize)
}

pub fn value_body(wire: &[u8]) -> &[u8] {
    let Some(len) = value_effective_len(wire) else {
        return &[];
    };
    let Some(end) = HEAD_LEN.checked_add(len) else {
        return &[];
    };
    if end > wire.len() {
        return &[];
    }
    &wire[HEAD_LEN..end]
}

pub fn value_is_ptr(wire: &[u8]) -> bool {
    value_langtype(wire).as_bytes().first() == Some(&b'*')
}

pub fn value_is_ext(wire: &[u8]) -> bool {
    value_langtype(wire).as_bytes().first() == Some(&b'@')
}

fn call_codec64(f: impl FnOnce(*mut *mut u8, *mut u64) -> c_int) -> Vec<u8> {
    let mut out: *mut u8 = std::ptr::null_mut();
    let mut out_len: u64 = 0;
    f(&mut out, &mut out_len);
    if out.is_null() || out_len == 0 || out_len > u32::MAX as u64 {
        if !out.is_null() {
            unsafe { libc::free(out as *mut c_void) };
        }
        return Vec::new();
    }
    let bytes = unsafe { std::slice::from_raw_parts(out, out_len as usize) }.to_vec();
    unsafe { libc::free(out as *mut c_void) };
    bytes
}

pub static EMPTY_CSTR: [u8; 1] = [0];

pub fn full_langtype(kind: &str, dims: &[i32]) -> String {
    if dims.is_empty() {
        return kind.to_string();
    }
    let d: Vec<String> = dims.iter().map(|x| x.to_string()).collect();
    format!("[{}]{kind}", d.join(","))
}

fn build_checked(f: impl FnOnce(*mut *mut u8, *mut u64) -> c_int,
                 want_langtype: &str, want_len: usize) -> Vec<u8> {
    let bytes = call_codec64(f);
    if bytes.is_empty() {
        return bytes;
    }
    let got_lt = value_langtype(&bytes);
    assert_eq!(got_lt, want_langtype,
               "constructor langtype mismatch: want {want_langtype:?}, wire has {got_lt:?}");
    let got_len = value_effective_len(&bytes)
        .unwrap_or_else(|| panic!("constructed wire has no effective length"));
    assert_eq!(got_len, want_len,
               "constructor effective length mismatch: want {want_len}, wire has {got_len}");
    bytes
}

pub fn xvalue_to_wire(kind: &str, raw: &[u8], dims: &[i32]) -> Vec<u8> {
    let lt = full_langtype(kind, dims);
    let c_lt = CString::new(lt.clone()).expect("langtype contains NUL");
    if !dims.is_empty() && !kind.contains('\u{b7}') && !kind.starts_with('/') {
        let d: Vec<u64> = dims.iter().map(|&x| x as u64).collect();
        return build_checked(
            |out, out_len| unsafe {
                pathlangXvalueHeadNewTensor(d.as_ptr(), d.len() as u32,
                                           raw.as_ptr(), raw.len() as u64,
                                           c_lt.as_ptr(), out, out_len)
            },
            &lt, raw.len());
    }
    if kind == "char" || kind.starts_with("char/") {
        return build_checked(
            |out, out_len| unsafe {
                pathlangXvalueHeadNewSlack(raw.as_ptr(), raw.len() as u64,
                                          raw.len() as u64, c_lt.as_ptr(),
                                          out, out_len)
            },
            &lt, raw.len());
    }
    if kind == "rwir" || kind == "def langtype" || (kind == "rwfunc" && !raw.is_empty()) {
        return build_checked(
            |out, out_len| unsafe {
                pathlangXvalueHeadNewCode(raw.as_ptr(), raw.len() as u64,
                                         raw.len() as u64, c_lt.as_ptr(),
                                         out, out_len)
            },
            &lt, raw.len());
    }
    build_checked(
        |out, out_len| unsafe {
            pathlangXvalueHeadNewScalar(raw.as_ptr(), raw.len() as u32,
                                       c_lt.as_ptr(), out, out_len)
        },
        &lt, raw.len())
}

pub fn xvalue_to_wire_untyped(kind: &str, raw: &[u8], dims: &[i32]) -> Vec<u8> {
    let empty = CString::new("").expect("empty");
    if !dims.is_empty() && !kind.contains('\u{b7}') && !kind.starts_with('/') {
        let d: Vec<u64> = dims.iter().map(|&x| x as u64).collect();
        return build_checked(
            |out, out_len| unsafe {
                pathlangXvalueHeadNewTensor(d.as_ptr(), d.len() as u32,
                                           raw.as_ptr(), raw.len() as u64,
                                           empty.as_ptr(), out, out_len)
            },
            "", raw.len());
    }
    if kind == "char" || kind.starts_with("char/") {
        return build_checked(
            |out, out_len| unsafe {
                pathlangXvalueHeadNewSlack(raw.as_ptr(), raw.len() as u64,
                                          raw.len() as u64, empty.as_ptr(),
                                          out, out_len)
            },
            "", raw.len());
    }
    build_checked(
        |out, out_len| unsafe {
            pathlangXvalueHeadNewScalar(raw.as_ptr(), raw.len() as u32,
                                       empty.as_ptr(), out, out_len)
        },
        "", raw.len())
}

pub fn none_wire() -> Vec<u8> {
    build_checked(|out, out_len| unsafe { pathlangXvalueHeadNewNone(out, out_len) },
                  "", 0)
}

pub fn ptr_wire(base_langtype: &str, path: &[u8]) -> Vec<u8> {
    let c_lt = CString::new(base_langtype).expect("langtype contains NUL");
    let want = format!("*{base_langtype}");
    build_checked(
        |out, out_len| unsafe {
            pathlangXvalueHeadNewPtr(path.as_ptr(), path.len() as u32,
                                    c_lt.as_ptr(), out, out_len)
        },
        &want, path.len())
}

pub fn ext_wire(base_langtype: &str, locator: &[u8]) -> Vec<u8> {
    let c_lt = CString::new(base_langtype).expect("langtype contains NUL");
    let want = format!("@{base_langtype}");
    build_checked(
        |out, out_len| unsafe {
            pathlangXvalueHeadNewExt(locator.as_ptr(), locator.len() as u32,
                                    c_lt.as_ptr(), out, out_len)
        },
        &want, locator.len())
}

pub fn langtype_of(h: *mut c_void, key: &str) -> String {
    let mut off: u64 = 0;
    let mut err = [0u8; 256];
    let rc = unsafe {
        pathspaceResolveOffset(
            h, cs(key).as_ptr(), 0, &mut off,
            err.as_mut_ptr() as *mut c_char, err.len() as u32,
        )
    };
    if rc != 0 {
        return String::new();
    }
    let base = unsafe { pathspaceXvalueInProcessAddress(h) };
    if base.is_null() {
        return String::new();
    }
    unsafe {
        let wire_at = base.add(off as usize);
        let bodylen = u64::from_le_bytes(*(wire_at as *const [u8; 8]));
        let attrslen = u16::from_le_bytes(*(wire_at.add(8) as *const [u8; 2])) as u64;
        let Some(total) = (HEAD_LEN as u64).checked_add(bodylen).and_then(|n| n.checked_add(attrslen)) else {
            return String::new();
        };
        let wire = std::slice::from_raw_parts(wire_at, total as usize);
        value_langtype(wire)
    }
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum RuntimecStatus {
    Success = 0,
    Yield = 1,
    Wait = 2,
    Unsupported = 3,
    NotImplemented = 4,
    Invalid = 5,
    OutOfMemory = 6,
    Error = 7,
    InsufficientCapacity = 8,
}
pub type runtimec_status_t = RuntimecStatus;

pub enum pathlang_runtime_t {}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pathlang_runtime_text_buffer_t {
    pub data: *mut c_char,
    pub capacity: usize,
    pub length: usize,
    pub required_length: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pathlang_runtime_error_t {
    pub status: runtimec_status_t,
    pub operation_status: runtimec_status_t,
    pub code: i64,
    pub message: pathlang_runtime_text_buffer_t,
}

pub type pathlang_runtime_extension_handler_t = Option<
    unsafe extern "C" fn(frame: *mut c_void, instruction: *const c_void, output_exit: *mut c_void) -> runtimec_status_t,
>;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pathlang_runtime_extension_entry_t {
    pub structure_size: u32,
    pub operation_identifier: u32,
    pub flags: u32,
    pub handler: pathlang_runtime_extension_handler_t,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pathlang_runtime_extension_module_t {
    pub module_name: *const c_char,
    pub entries: *const pathlang_runtime_extension_entry_t,
    pub entry_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pathlang_runtime_create_request_t {
    pub structure_size: usize,
    pub data_source_name: *const c_char,
    pub instance_identifier: u64,
    pub extensions: *const pathlang_runtime_extension_module_t,
    pub extension_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum pathlang_runtime_entry_kind_t {
    Function = 0,
    ProgramCounter = 1,
    Vthread = 2,
    Bootstrap = 3,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pathlang_runtime_execute_request_t {
    pub entry_kind: pathlang_runtime_entry_kind_t,
    pub entry_name: *const c_char,
    pub arguments: *const *const c_char,
    pub argument_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pathlang_runtime_execute_result_t {
    pub value: pathlang_runtime_text_buffer_t,
    pub program_counter: pathlang_runtime_text_buffer_t,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum pathlang_runtime_exit_kind_t {
    None = 0,
    Error = 1,
    Yield = 2,
}

pub const RUNTIMEC_EXTENSION_FLAG_EFFECT: u32 = 1 << 0;
pub const RUNTIMEC_EXTENSION_FLAG_MAY_YIELD: u32 = 1 << 1;

#[cfg(not(feature = "embed"))]
unsafe extern "C" {
    
    pub fn pathspaceConnect(dsn: *const c_char) -> *mut c_void;
    
    pub fn pathspaceXvalueInProcessAddress(h: *mut c_void) -> *mut u8;
    
    pub fn pathspaceResolveOffset(
        h: *mut c_void,
        key: *const c_char,
        parent_key_anchor: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    pub fn pathspaceRealloc(
        h: *mut c_void,
        key: *const c_char,
        body_length: u64,
        attr_length: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    pub fn pathspaceFree(h: *mut c_void, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    
    pub fn pathspaceDeltree(
        h: *mut c_void,
        key: *const c_char,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    pub fn pathspaceListlen(
        h: *mut c_void,
        key: *const c_char,
        out_count: *mut i64,
        out_max_name_length: *mut u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    pub fn pathspaceList(
        h: *mut c_void,
        key: *const c_char,
        out_names: *mut c_char,
        out_capacity: u32,
        out_length: *mut u32,
        out_count: *mut u32,
        out_generation: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    pub fn pathlangXvalueHeadNewNone(out: *mut *mut u8, out_len: *mut u64) -> c_int;
    
    pub fn pathspaceXvalueGetAttr(
        wire: *const u8,
        wire_len: u64,
        tag: u8,
        out_data: *mut *const u8,
        out_len: *mut u16,
    ) -> c_int;
    
    pub fn pathspaceXvalueHeadEffectiveLen(
        wire: *const u8,
        wire_len: u64,
        out: *mut u64,
    ) -> c_int;
    pub fn pathlangXvalueHeadNewScalar(
        raw: *const u8,
        raw_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn pathlangXvalueHeadNewSlack(
        data: *const u8,
        data_len: u64,
        cap: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn pathlangXvalueHeadNewTensor(
        dims: *const u64,
        ndim: u32,
        raw: *const u8,
        raw_len: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn pathlangXvalueHeadNewCode(
        body: *const u8,
        body_len: u64,
        cap: u64,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn pathlangXvalueHeadNewPtr(
        path: *const u8,
        path_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;
    pub fn pathlangXvalueHeadNewExt(
        locator: *const u8,
        locator_len: u32,
        langtype: *const c_char,
        out: *mut *mut u8,
        out_len: *mut u64,
    ) -> c_int;

    pub fn pathlang_runtime_create(
        request: *const pathlang_runtime_create_request_t,
        output: *mut *mut pathlang_runtime_t,
        error: *mut pathlang_runtime_error_t,
    ) -> runtimec_status_t;
    pub fn pathlang_runtime_destroy(
        runtime: *mut pathlang_runtime_t,
        error: *mut pathlang_runtime_error_t,
    ) -> runtimec_status_t;
    pub fn pathlang_runtime_execute(
        runtime: *mut pathlang_runtime_t,
        request: *const pathlang_runtime_execute_request_t,
        output: *mut pathlang_runtime_execute_result_t,
        error: *mut pathlang_runtime_error_t,
    ) -> runtimec_status_t;

    pub fn pathlang_runtime_read_argument_text(
        frame: *const c_void,
        instruction: *const c_void,
        index: usize,
        output: *mut pathlang_runtime_text_buffer_t,
        error: *mut pathlang_runtime_error_t,
    ) -> runtimec_status_t;
    pub fn pathlang_runtime_write_argument_wire(
        frame: *mut c_void,
        instruction: *const c_void,
        index: usize,
        data: *const u8,
        size: usize,
        error: *mut pathlang_runtime_error_t,
    ) -> runtimec_status_t;
    pub fn pathlang_runtime_set_exit(
        output_exit: *mut c_void,
        kind: pathlang_runtime_exit_kind_t,
        detail_code: i64,
        message: *const c_char,
        error: *mut pathlang_runtime_error_t,
    ) -> runtimec_status_t;

    pub fn pathlangRuntimeConnect(dsn: *const c_char) -> *mut c_void;
    pub fn pathlangRuntimeDisconnect(rt: *mut c_void);
    
    pub fn pathlangRuntimePathspaceHandle(rt: *mut c_void) -> *mut c_void;
    pub fn pathlangRuntimeBootstrap(
        rt: *mut c_void,
        funcname: *const c_char,
        args: *const *const c_char,
        nargs: c_int,
    ) -> *mut c_char;
    pub fn pathlangRuntimeExecuteVthread(
        rt: *mut c_void,
        vid: *const c_char,
        out_pc: *mut *mut c_char,
    ) -> c_int;

    pub fn pathlangDefRwir(
        pathspace: *mut c_void,
        opcode: *const c_char,
        rp: *const *const c_char,
        nr: c_int,
        wp: *const *const c_char,
        nw: c_int,
    ) -> c_int;
    pub fn pathlangRwirextParams(pathspace: *mut c_void, pc: *const c_char) -> *mut c_char;
    pub fn pathlangRwirextResolveRead(
        pathspace: *mut c_void,
        pc: *const c_char,
        idx: c_int,
    ) -> *mut c_char;
    pub fn pathlangRwirextResolveReadPath(
        pathspace: *mut c_void,
        pc: *const c_char,
        idx: c_int,
    ) -> *mut c_char;
    pub fn pathlangRwirextResolveWrite(
        pathspace: *mut c_void,
        pc: *const c_char,
        idx: c_int,
    ) -> *mut c_char;
    pub fn pathlangRwirextNextPc(pc: *const c_char) -> *mut c_char;
    
    pub fn pathlangRwirextHandoff(
        pathspace: *mut c_void,
        vtid: *const c_char,
        pc: *const c_char,
    ) -> c_int;

    pub fn pathlangLayoutFile(
        path: *const c_char,
        dsn: *const c_char,
        entry: *mut c_char,
        entry_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn pathlangLayoutCode(
        src: *const c_char,
        dsn: *const c_char,
        entry: *mut c_char,
        entry_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn pathlangLayoutFormat(
        src: *const c_char,
        out: *mut c_char,
        out_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn pathlangLayoutVet(src: *const c_char, err: *mut c_char, err_cap: u32) -> c_int;
    pub fn pathlangLayoutPrintlib(
        lib: *const c_char,
        dsn: *const c_char,
        out: *mut c_char,
        out_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    pub fn pathlangLayoutPrintstack(
        vid: *const c_char,
        dsn: *const c_char,
        out: *mut c_char,
        out_cap: u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
}

#[cfg(feature = "embed")]
include!(concat!(env!("OUT_DIR"), "/embed_shim.rs"));

pub fn cs(s: &str) -> CString {
    CString::new(s).unwrap_or_else(|_| CString::new("").unwrap())
}

pub fn take(p: *mut c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    let s = unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned();
    unsafe { libc::free(p as *mut c_void) };
    s
}

pub fn cbuf(buf: &[u8]) -> String {
    let n = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    String::from_utf8_lossy(&buf[..n]).into_owned()
}
