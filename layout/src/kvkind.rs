
use super::ffi;

pub const KIND_CHAR: &str = "char/utf32";

pub const KIND_DEF_STRUCT: &str = "def struct";

pub const KIND_RWIR: &str = "rwir";
pub const KIND_RWFUNC: &str = "rwfunc";
pub const KIND_DEF_RWIR: &str = "def rwir";
pub const KIND_DEF_LANGTYPE: &str = "def langtype";

pub fn is_none(data: &[u8]) -> bool {
    data.is_empty()
}

pub fn is_map_langtype(kx: &str) -> bool {
    kx.starts_with('\'')
}

pub fn parse_langtype(kx: &str) -> (Vec<i32>, String) {
    
    debug_assert!(
        !is_map_langtype(kx),
        "parse_langtype received a map langtype {kx:?}; the caller must ask is_map_langtype first"
    );
    
    if kx.starts_with('[') {
        match kx.find(']') {
            Some(end) => {
                let inner = &kx[1..end];
                if inner.split(',').all(|d| {
                    let d = d.trim();
                    d.is_empty() || d == "?" || d.parse::<i32>().is_ok()
                }) {
                    return (
                        inner
                            .split(',')
                            .filter(|d| !d.is_empty())
                            .map(|d| d.parse().unwrap_or(0))
                            .collect(),
                        kx[end + 1..].to_string(),
                    );
                }
                (Vec::new(), kx.to_string())
            }
            None => (Vec::new(), kx.to_string()),
        }
    } else {
        (Vec::new(), kx.to_string())
    }
}

pub fn langtype(data: &[u8], lt: &str) -> String {
    if data.is_empty() {
        String::new()
    } else {
        lt.to_string()
    }
}

pub fn kind(data: &[u8], lt: &str) -> String {
    if data.is_empty() {
        return String::new();
    }
    parse_langtype(lt).1
}

pub fn is_ptr(data: &[u8], langtype: &str) -> bool {
    !data.is_empty() && ffi::langtype_is_pointer(langtype)
}

pub fn array_len(data: &[u8], lt: &str) -> i32 {
    if data.is_empty() {
        return 0;
    }
    let dims = parse_langtype(lt).0;
    if dims.is_empty() {
        1
    } else {
        dims.iter().product()
    }
}

pub fn body<'a>(data: &'a [u8]) -> &'a [u8] {
    ffi::xh_body_slice(data)
}

pub fn ptr_target(data: &[u8]) -> String {
    String::from_utf8_lossy(body(data)).into_owned()
}

pub fn value_string(data: &[u8]) -> String {
    String::from_utf8_lossy(body(data)).into_owned()
}

pub fn display(data: &[u8], lt: &str) -> String {
    if data.is_empty() {
        return "None".to_string();
    }
    let (_, k) = parse_langtype(lt);
    if k.is_empty() {
        return "None".to_string();
    }
    let b = body(data);
    if ffi::langtype_is_pointer(lt) {
        return format!("→{}:{}", String::from_utf8_lossy(b), k);
    }
    format!("{}:{}", k, plain_value(&k, b))
}

fn le_u32(b: &[u8]) -> u32 {
    b.iter()
        .take(4)
        .enumerate()
        .fold(0, |a, (i, &x)| a | ((x as u32) << (8 * i)))
}
fn le_u64(b: &[u8]) -> u64 {
    b.iter()
        .take(8)
        .enumerate()
        .fold(0u64, |a, (i, &x)| a | ((x as u64) << (8 * i)))
}
fn fmt_float(v: f64) -> String {
    let s = format!("{v}");
    if s.contains('.') {
        s
    } else {
        format!("{s}.0")
    }
}
fn arr<const N: usize>(b: &[u8]) -> [u8; N] {
    let mut a = [0u8; N];
    let n = b.len().min(N);
    a[..n].copy_from_slice(&b[..n]);
    a
}

fn plain_value(k: &str, b: &[u8]) -> String {
    match k {
        "bool" => (b.first().copied().unwrap_or(0) != 0).to_string(),
        "int8" => (b.first().map(|&x| x as i8).unwrap_or(0) as i64).to_string(),
        "int16" => (i16::from_le_bytes(arr(b)) as i64).to_string(),
        "int32" => (i32::from_le_bytes(arr(b)) as i64).to_string(),
        "int64" => i64::from_le_bytes(arr(b)).to_string(),
        "uint8" => b.first().copied().unwrap_or(0).to_string(),
        "uint16" => u16::from_le_bytes(arr(b)).to_string(),
        "uint32" => le_u32(b).to_string(),
        "uint64" => le_u64(b).to_string(),
        "float32" => fmt_float(f32::from_le_bytes(arr(b)) as f64),
        "float64" => fmt_float(f64::from_le_bytes(arr(b))),
        "char/utf8" | "char/ascii" => String::from_utf8_lossy(b).into_owned(),
        "char/utf32" => b
            .chunks(4)
            .map(|c| char::from_u32(le_u32(c)).unwrap_or('\u{FFFD}'))
            .collect(),
        
        "rwir" | "rwfunc" | "def rwir" => {
            let (nr, nw, dynamic) = if b.len() >= 5 {
                (
                    u16::from_le_bytes([b[0], b[1]]),
                    u16::from_le_bytes([b[2], b[3]]),
                    b[4] != 0,
                )
            } else {
                (0, 0, false)
            };
            let sig = String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned();
            let var = if dynamic { "..." } else { "" };
            if nr == 0 && nw == 0 {
                sig
            } else {
                format!("(nr={nr},nw={nw}{var}) {sig}")
            }
        }
        _ => String::from_utf8_lossy(b).into_owned(),
    }
}

pub fn is_char_kind(k: &str) -> bool {
    k.starts_with("char/")
}

fn counts_body(nr: i32, nw: i32, dynamic: bool) -> Result<Vec<u8>, String> {
    let mut raw = Vec::with_capacity(5);
    let nr = u16::try_from(nr).map_err(|_| format!("read count {nr} exceeds 65535"))?;
    let nw = u16::try_from(nw).map_err(|_| format!("write count {nw} exceeds 65535"))?;
    raw.extend_from_slice(&nr.to_le_bytes());
    raw.extend_from_slice(&nw.to_le_bytes());
    raw.push(dynamic as u8);
    Ok(raw)
}

pub fn new_rwir(nr: i32, nw: i32, sig: &str) -> Result<Vec<u8>, String> {
    let mut raw = counts_body(nr, nw, false)?;
    raw.extend_from_slice(sig.as_bytes());
    ffi::xvalue_to_wire(KIND_RWIR, &raw, 1)
}

pub fn new_rwfunc_call(nr: i32, nw: i32, sig: &str) -> Result<Vec<u8>, String> {
    let mut raw = counts_body(nr, nw, false)?;
    raw.extend_from_slice(sig.as_bytes());
    ffi::xvalue_to_wire(KIND_RWFUNC, &raw, 1)
}

pub fn new_typed_rwir(name: &str, langtype: &str) -> Result<Vec<u8>, String> {
    let mut raw = counts_body(0, 0, false)?;
    raw.extend_from_slice(name.as_bytes());
    raw.push(0);
    raw.extend_from_slice(langtype.as_bytes());
    ffi::xvalue_to_wire(KIND_RWIR, &raw, 1)
}

pub fn new_defrwir(nr: i32, nw: i32, dynamic: bool) -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_DEF_RWIR, &counts_body(nr, nw, dynamic)?, 1)
}

pub const INPLACE_SUFFIX: &str = " inplace";

pub fn new_def_langtype(langtype: &str) -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_DEF_LANGTYPE, langtype.as_bytes(), 1)
}

pub fn new_def_langtype_with_inplace(langtype: &str, inplace: bool) -> Result<Vec<u8>, String> {
    let clean = langtype.strip_suffix(INPLACE_SUFFIX).unwrap_or(langtype);
    if inplace {
        let mut raw = String::with_capacity(clean.len() + INPLACE_SUFFIX.len());
        raw.push_str(clean);
        raw.push_str(INPLACE_SUFFIX);
        ffi::xvalue_to_wire(KIND_DEF_LANGTYPE, raw.as_bytes(), 1)
    } else {
        new_def_langtype(clean)
    }
}

pub fn def_langtype_full(data: &[u8]) -> Option<(String, bool)> {
    if data.is_empty() || ffi::xh_is_none(data) {
        return None;
    }
    let b = body(data);
    if b.is_empty() {
        return None;
    }
    let raw = String::from_utf8_lossy(b);
    match raw.strip_suffix(INPLACE_SUFFIX) {
        Some(ty) => Some((ty.to_string(), true)),
        None => Some((raw.into_owned(), false)),
    }
}

pub fn new_def_param(langtype: &str) -> Result<Vec<u8>, String> {
    new_def_param_with_inplace(langtype, false)
}

pub fn new_def_param_with_inplace(langtype: &str, inplace: bool) -> Result<Vec<u8>, String> {
    new_def_langtype_with_inplace(langtype, inplace)
}

pub fn def_param_full(data: &[u8]) -> Option<(String, bool)> {
    def_langtype_full(data)
}

pub fn new_struct() -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_DEF_STRUCT, &[], 1)
}

pub fn new_rwfunc_dir() -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_RWFUNC, &[], 1)
}

pub fn new_rwfunc_anchor(nr: i32, nw: i32, dynamic: bool) -> Result<Vec<u8>, String> {
    ffi::xvalue_to_wire(KIND_RWFUNC, &counts_body(nr, nw, dynamic)?, 1)
}

pub fn rwfunc_num_reads(body: &[u8]) -> i32 {
    if body.len() < 2 {
        return 0;
    }
    u16::from_le_bytes([body[0], body[1]]) as i32
}

pub fn rwfunc_num_writes(body: &[u8]) -> i32 {
    if body.len() < 4 {
        return 0;
    }
    u16::from_le_bytes([body[2], body[3]]) as i32
}

pub fn counts(data: &[u8]) -> (i32, i32, bool) {
    if data.is_empty() {
        return (0, 0, false);
    }
    let b = body(data);
    if b.len() >= 5 {
        (
            u16::from_le_bytes([b[0], b[1]]) as i32,
            u16::from_le_bytes([b[2], b[3]]) as i32,
            b[4] != 0,
        )
    } else {
        (0, 0, false)
    }
}

pub fn plain(data: &[u8], lt: &str) -> String {
    if data.is_empty() {
        return String::new();
    }
    let (_, k) = parse_langtype(lt);
    plain_value(&k, body(data))
}

pub fn write_slot_name(data: &[u8]) -> (String, String) {
    if data.is_empty() {
        return (String::new(), String::new());
    }
    let b = body(data);
    if b.len() > 5 {
        if let Some(split) = b[5..].iter().position(|&x| x == 0) {
            let name = String::from_utf8_lossy(&b[5..5 + split]).into_owned();
            let ty = String::from_utf8_lossy(&b[6 + split..]).into_owned();
            return (name, ty);
        }
    }
    (rwir_sig(data), String::new())
}

pub fn rwir_sig(data: &[u8]) -> String {
    if data.is_empty() {
        return String::new();
    }
    let b = body(data);
    let text = &b[5.min(b.len())..];
    let end = text.iter().position(|&x| x == 0).unwrap_or(text.len());
    String::from_utf8_lossy(&text[..end]).into_owned()
}

#[cfg(test)]
mod map_langtype_tests {
    use super::{is_map_langtype, parse_langtype};

    #[test]
    fn legal_map_types_are_map() {
        
        for text in [
            "'int64'int64",                    
            "'[]char/utf8'int64",              
            "'[]char/utf32'int64",             
            "'[int64]'int64",                  
            "'[float64,float64]'[]char/utf32", 
            "'[int32,int32]'int64",            
            "'[int64]·[]char/utf8'int64",      
            "'bool'int64",                     
        ] {
            assert!(is_map_langtype(text), "expected map: {text}");
        }
    }

    #[test]
    fn non_map_types_are_not_map() {
        
        for text in ["[]char/utf8", "[2]float64", "int64", "char/utf32"] {
            assert!(!is_map_langtype(text), "expected non-map: {text}");
        }
    }

    #[test]
    fn illegal_map_spellings_are_recognized_as_map_but_rejected_downstream() {
        
        for text in [
            "''int64",       
            "''int64'int64", 
            "'int64'",       
            "'int64",        
            "'foo'int64",    
        ] {
            assert!(
                is_map_langtype(text),
                "starts with quote, so it is map-shaped: {text}"
            );
        }
        
        assert!(!is_map_langtype("int64'int64"));
    }

    #[test]
    fn utf8_and_utf32_formatters_yield_the_same_access_key() {
        
        assert!(is_map_langtype("'[]char/utf8'int64"));
        assert!(is_map_langtype("'[]char/utf32'int64"));
    }

    #[test]
    fn parse_langtype_never_receives_a_map_type() {
        
        let (dims, base) = parse_langtype("[2]float64");
        assert_eq!(dims, vec![2]);
        assert_eq!(base, "float64");
    }
}
