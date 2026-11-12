
use serde_json::{Map, Value};

use crate::engine::{Engine, KvResult};
use crate::ffi::*;

const SEP: &str = "·";
const DIR_SUF: &str = "/";
const OBJECT_TYPE: &str = "[]char/utf32·";
const ARRAY_TYPE: &str = "[int64]·";

pub fn to(eng: &Engine, pc: &str) -> KvResult {
    let root = root_of(eng, pc, 0);
    if root.is_empty() {
        return Ok(());
    }
    let out = read_value(eng, &root).to_string();
    eng.set_kv(&eng.write0(pc), &out)
}

fn ptr_target(eng: &Engine, path: &str) -> String {
    let xv = eng.get_xvalue(path);
    if !xv.is_empty() && value_is_ptr(&xv) {
        let t = String::from_utf8_lossy(value_body(&xv)).into_owned();
        if !t.is_empty() {
            return t;
        }
    }
    path.to_string()
}

fn root_of(eng: &Engine, pc: &str, idx: i32) -> String {
    let names = params(eng, pc);
    let rname = names.get((idx + 1) as usize).map(String::as_str).unwrap_or("");
    if rname.starts_with('/') && !rname.contains("·*") {
        return ptr_target(eng, rname);
    }
    let v = eng.read_at(pc, idx);
    if v.starts_with('/') {
        return ptr_target(eng, &v);
    }
    let p = take(unsafe { kvlangRwirextResolveReadPath(eng.kv, cs(pc).as_ptr(), idx) });
    if p.is_empty() {
        return String::new();
    }
    ptr_target(eng, &p)
}

pub fn from(eng: &Engine, pc: &str) -> KvResult {
    let names = params(eng, pc);
    let src = eng.read0(pc);
    let wname = names.get(2).map(String::as_str).unwrap_or("");
    let root = if wname.starts_with('/') && !wname.contains("·*") {
        wname.to_string()
    } else {
        eng.write0(pc)
    };
    if let Ok(v) = serde_json::from_str::<Value>(&src) {
        eng.del_tree(&root);
        write_value(eng, &root, &v)?;
    }
    Ok(())
}

fn params(eng: &Engine, pc: &str) -> Vec<String> {
    let s = take(unsafe { kvlangRwirextParams(eng.kv, cs(pc).as_ptr()) });
    s.split('\n').map(str::to_string).collect()
}

fn read_value(eng: &Engine, path: &str) -> Value {
    let (kind, raw, arr_len) = parse_xvalue(eng, path, &eng.get_xvalue(path));
    if path != "/" && (kind.contains(SEP) || !member_names(eng, path).is_empty()) {
        return read_container(eng, path, &kind);
    }
    if !eng.list_kv(&format!("{path}{DIR_SUF}")).is_empty() {
        return read_dir(eng, path);
    }
    if kind.is_empty() {
        return Value::Null; 
    }
    xvalue_to_json(&kind, &raw, arr_len)
}

fn read_dir(eng: &Engine, path: &str) -> Value {
    let prefix = if path == "/" {
        "/".to_string()
    } else {
        format!("{path}{DIR_SUF}")
    };
    let mut map = Map::new();
    for name in eng.list_kv(&prefix) {
        let key = name.trim_end_matches('/').trim_end_matches(SEP).to_string();
        if key.is_empty() {
            continue;
        }
        let child = if path == "/" {
            format!("/{key}")
        } else {
            format!("{path}{DIR_SUF}{key}")
        };
        map.insert(key, read_value(eng, &child));
    }
    Value::Object(map)
}

fn member_names(eng: &Engine, path: &str) -> Vec<String> {
    let mut names = Vec::new();
    for n in eng.list_kv(&format!("{path}{SEP}")) {
        let n = n.trim_end_matches(SEP).to_string();
        if !n.is_empty() && !names.contains(&n) {
            names.push(n);
        }
    }
    names
}

fn read_container(eng: &Engine, path: &str, langtype: &str) -> Value {
    let names = member_names(eng, path);
    if !names.is_empty()
        && names.iter().all(|n| {
            n.strip_prefix('[')
                .and_then(|r| r.strip_suffix(']'))
                .is_some_and(|i| i.parse::<usize>().is_ok())
        })
    {
        read_arr(eng, path, &names)
    } else if names.is_empty() && langtype.starts_with("[int64]·") {
        Value::Array(Vec::new())
    } else {
        read_obj(eng, path, &names)
    }
}

fn read_obj(eng: &Engine, path: &str, names: &[String]) -> Value {
    let mut map = Map::new();
    for name in names {
        map.insert(name.clone(), read_value(eng, &format!("{path}{SEP}{name}")));
    }
    Value::Object(map)
}

fn read_arr(eng: &Engine, path: &str, names: &[String]) -> Value {
    let mut idxs: Vec<usize> = Vec::new();
    for n in names {
        let s = n.trim_start_matches('[').trim_end_matches(']');
        if let Ok(i) = s.parse::<usize>() {
            idxs.push(i);
        }
    }
    idxs.sort_unstable();
    Value::Array(
        idxs.iter()
            .map(|i| read_value(eng, &format!("{path}{SEP}[{i}]")))
            .collect(),
    )
}

fn parse_xvalue(eng: &Engine, path: &str, data: &[u8]) -> (String, Vec<u8>, usize) {
    if data.is_empty() {
        return (String::new(), Vec::new(), 1);
    }
    let kx = crate::ffi::langtype_of(eng.kv, path);
    let (dims, kind) = parse_langtype(&kx);
    let raw = value_body(data).to_vec();
    
    let arr_len = dims
        .iter()
        .fold(1usize, |n, &d| n.saturating_mul(d.max(1) as usize));
    (kind, raw, arr_len)
}

fn xvalue_to_json(kind: &str, raw: &[u8], arr_len: usize) -> Value {
    let es = elem_size(kind);
    let elems = |f: fn(&[u8]) -> Value| -> Value {
        Value::Array(raw.chunks_exact(es.max(1)).take(arr_len).map(f).collect())
    };
    match kind {
        "bool" => {
            if arr_len > 1 {
                elems(|c| Value::Bool(c[0] != 0))
            } else {
                Value::Bool(raw.first().map(|&b| b != 0).unwrap_or(false))
            }
        }
        "int8" | "int16" | "int32" | "int64" | "uint8" | "uint16" | "uint32" | "uint64" => {
            if arr_len > 1 {
                elems(|c| Value::from(read_int(c)))
            } else {
                Value::from(read_int(raw))
            }
        }
        "float32" | "float64" => {
            if arr_len > 1 {
                elems(|c| Value::from(float_from(c)))
            } else {
                Value::from(float_from(raw))
            }
        }
        "char/utf8" | "char/ascii" => Value::String(String::from_utf8_lossy(raw).into_owned()),
        "char/utf32" => Value::String(utf32_to_string(raw)),
        _ => Value::String(String::from_utf8_lossy(raw).into_owned()),
    }
}

fn elem_size(kind: &str) -> usize {
    match kind {
        "int8" | "uint8" | "bool" => 1,
        "int16" | "uint16" => 2,
        "int32" | "uint32" | "float32" => 4,
        "int64" | "uint64" | "float64" => 8,
        _ => 0,
    }
}

fn read_int(raw: &[u8]) -> i64 {
    match raw.len() {
        1 => raw[0] as i8 as i64,
        2 => i16::from_le_bytes(raw.try_into().unwrap()) as i64,
        4 => i32::from_le_bytes(raw.try_into().unwrap()) as i64,
        8 => i64::from_le_bytes(raw.try_into().unwrap()),
        _ => 0,
    }
}

fn float_from(raw: &[u8]) -> f64 {
    match raw.len() {
        4 => f32::from_le_bytes(raw.try_into().unwrap()) as f64,
        8 => f64::from_le_bytes(raw.try_into().unwrap()),
        _ => 0.0,
    }
}

fn utf32_to_string(raw: &[u8]) -> String {
    raw.chunks_exact(4)
        .map(|c| char::from_u32(u32::from_le_bytes(c.try_into().unwrap())).unwrap_or('\u{FFFD}'))
        .collect()
}

fn write_value(eng: &Engine, path: &str, v: &Value) -> KvResult {
    match v {
        Value::Object(m) => write_obj(eng, path, m),
        Value::Array(arr) => write_arr(eng, path, arr),
        Value::Null => eng.set_xvalue(path, &xvalue_to_wire("None", &[], &[])),
        _ => eng.set_xvalue(path, &value_to_xvalue(v)),
    }
}

fn write_obj(eng: &Engine, path: &str, m: &Map<String, Value>) -> KvResult {
    eng.set_xvalue(path, &mk_obj_value())?;
    for k in m.keys() {
        write_value(eng, &format!("{path}{SEP}{k}"), &m[k])?;
    }
    Ok(())
}

fn write_arr(eng: &Engine, path: &str, arr: &[Value]) -> KvResult {
    eng.set_xvalue(path, &mk_map_value())?;
    for (i, v) in arr.iter().enumerate() {
        write_value(eng, &format!("{path}{SEP}[{i}]"), v)?;
    }
    Ok(())
}

fn value_to_xvalue(v: &Value) -> Vec<u8> {
    match v {
        Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                xvalue_to_wire("int64", &i.to_le_bytes(), &[])
            } else {
                xvalue_to_wire("float64", &n.as_f64().unwrap_or(0.0).to_le_bytes(), &[])
            }
        }
        Value::Bool(b) => xvalue_to_wire("bool", &[*b as u8], &[]),
        Value::String(s) => new_char_byte(s.as_bytes()),
        _ => Vec::new(),
    }
}

fn mk_obj_value() -> Vec<u8> {
    xvalue_to_wire(OBJECT_TYPE, &[], &[])
}

fn mk_map_value() -> Vec<u8> {
    xvalue_to_wire(ARRAY_TYPE, &[], &[])
}

fn parse_langtype(kx: &str) -> (Vec<i32>, String) {
    if kx.contains(SEP) {
        return (Vec::new(), kx.to_string());
    }
    if kx.starts_with('[') {
        match kx.find(']') {
            Some(end) => (
                kx[1..end]
                    .split(',')
                    .filter(|d| !d.is_empty())
                    .map(|d| d.parse().unwrap_or(0))
                    .collect(),
                kx[end + 1..].to_string(),
            ),
            None => (Vec::new(), kx.to_string()),
        }
    } else {
        (Vec::new(), kx.to_string())
    }
}

fn new_char_byte(bytes: &[u8]) -> Vec<u8> {
    let utf32: Vec<u32> = String::from_utf8_lossy(bytes)
        .chars()
        .map(|c| c as u32)
        .collect();
    let raw: Vec<u8> = utf32.iter().flat_map(|v| v.to_le_bytes()).collect();
    let dims = [utf32.len() as i32];
    xvalue_to_wire("char/utf32", &raw, &dims)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::c_char;

    fn i64_xvalue(vals: &[i64]) -> Vec<u8> {
        let mut raw = Vec::with_capacity(vals.len() * 8);
        for v in vals {
            raw.extend_from_slice(&v.to_le_bytes());
        }
        let d = [vals.len() as i32];
        let ds: &[i32] = if vals.len() > 1 { &d } else { &[] };
        xvalue_to_wire("int64", &raw, ds)
    }

    fn test_engine() -> Engine {
        static NEXT: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
        let n = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let dsn = format!("fs:///tmp/kvlang-json-{}-{n}", std::process::id());
        let kv = unsafe { kvspaceConnect(cs(&dsn).as_ptr()) };
        assert!(!kv.is_null());
        let mut err = [0u8; 256];
        unsafe { kvspaceClear(kv, err.as_mut_ptr() as *mut c_char, 256) };
        Engine {
            rt: std::ptr::null_mut(),
            kv,
            dsn,
            ext: None,
        }
    }

    #[test]
    fn roundtrip() {
        let eng = test_engine();
        let v: Value = serde_json::from_str(
            r#"{"active":true,"age":42,"grp":{"c":1,"d":2},"name":"alice",
                "items":[{"id":1,"label":"one"},{"id":2,"label":"two"}],
                "scat":[10,20,30],"score":3.14}"#,
        )
        .unwrap();
        write_value(&eng, "/data", &v).unwrap();

        let j = read_value(&eng, "/data").to_string();
        assert_eq!(
            j,
            r#"{"active":true,"age":42,"grp":{"c":1,"d":2},"items":[{"id":1,"label":"one"},{"id":2,"label":"two"}],"name":"alice","scat":[10,20,30],"score":3.14}"#
        );
    }

    #[test]
    fn native_data_to() {
        let eng = test_engine();
        eng.set_xvalue("/data", &mk_obj_value()).unwrap();
        eng.set_xvalue("/data·age", &i64_xvalue(&[42])).unwrap();
        eng.set_xvalue("/data·name", &new_char_byte(b"alice")).unwrap();
        eng.set_xvalue("/data·scat", &mk_map_value()).unwrap();
        eng.set_xvalue("/data·scat·[0]", &i64_xvalue(&[10])).unwrap();
        eng.set_xvalue("/data·scat·[1]", &i64_xvalue(&[20])).unwrap();
        eng.set_xvalue("/data·scat·[2]", &i64_xvalue(&[30])).unwrap();

        let j = read_value(&eng, "/data").to_string();
        assert_eq!(j, r#"{"age":42,"name":"alice","scat":[10,20,30]}"#);
    }

    #[test]
    fn struct_and_local_container_to_json() {
        let eng = test_engine();
        
        let prof_wire = xvalue_to_wire("/lib/Profile", &[], &[]);
        eng.set_xvalue("/vthread/1/[1]/profile", &prof_wire).unwrap();
        eng.set_xvalue("/vthread/1/[1]/profile·name", &new_char_byte(b"alice"))
            .unwrap();
        eng.set_xvalue("/vthread/1/[1]/profile·age", &i64_xvalue(&[42]))
            .unwrap();
        eng.set_xvalue("/vthread/1/[1]/profile·nums", &i64_xvalue(&[1, 2, 3]))
            .unwrap();
        assert_eq!(
            read_value(&eng, "/vthread/1/[1]/profile").to_string(),
            r#"{"age":42,"name":"alice","nums":[1,2,3]}"#
        );
    }

    #[test]
    fn short_body_vs_langtype_dims() {
        let eng = test_engine();
        let one = |dims: &[i32]| xvalue_to_wire("int64", &7i64.to_le_bytes(), dims);
        eng.set_xvalue("/short", &one(&[1000])).unwrap();
        assert_eq!(read_value(&eng, "/short").to_string(), "[7]");
        eng.set_xvalue("/huge", &one(&[2147483647, 2147483647, 2147483647])).unwrap();
        assert_eq!(read_value(&eng, "/huge").to_string(), "[7]");
        let b = xvalue_to_wire("bool", &[1], &[64]);
        eng.set_xvalue("/flags", &b).unwrap();
        assert_eq!(read_value(&eng, "/flags").to_string(), "[true]");
        
        assert_eq!(xvalue_to_json("float64", &[], 5).to_string(), "[]");
        assert_eq!(xvalue_to_json("int32", &[1, 0, 0, 0, 9], usize::MAX).to_string(), "[1]");
    }
}
