//! JSON conversion over physical KVSpace members.

use serde_json::{Map, Value};

use crate::engine::Engine;
use crate::ffi::*;

const SEP: &str = "·";
const DIR_SUF: &str = "/";
const OBJECT_TYPE: &str = "[]char/utf32·any";
const ARRAY_TYPE: &str = "[int64]·any";

pub fn to(eng: &Engine, pc: &str) {
    let names = params(eng, pc);
    let root = if names[1].starts_with('/') {
        names[1].clone()
    } else {
        eng.read0(pc)
    };
    let out = read_value(eng, &root).to_string();
    eng.set_kv(&eng.write0(pc), &out);
}

pub fn from(eng: &Engine, pc: &str) {
    let names = params(eng, pc);
    let src = eng.read0(pc);
    let root = if names[2].starts_with('/') {
        names[2].clone()
    } else {
        eng.write0(pc)
    };
    if let Ok(v) = serde_json::from_str::<Value>(&src) {
        eng.del_tree(&root);
        write_value(eng, &root, &v);
    }
}

fn params(eng: &Engine, pc: &str) -> Vec<String> {
    let s = take(unsafe { kvlangRwirextParams(eng.kv, cs(pc).as_ptr()) });
    s.lines().map(str::to_string).collect()
}

fn read_value(eng: &Engine, path: &str) -> Value {
    let (kind, raw, arr_len) = parse_tlv(&eng.get_tlv(path));
    if kind.contains(SEP) {
        return read_container(eng, path, &kind);
    }
    if !eng.list_kv(&format!("{path}{DIR_SUF}")).is_empty() {
        return read_dir(eng, path);
    }
    if kind.is_empty() {
        return Value::Null; // None → JSON null
    }
    tlv_to_json(&kind, &raw, arr_len)
}

fn read_dir(eng: &Engine, path: &str) -> Value {
    let mut map = Map::new();
    for name in eng.list_kv(&format!("{path}{DIR_SUF}")) {
        let key = name.trim_end_matches('/').to_string();
        map.insert(
            key.clone(),
            read_value(eng, &format!("{path}{DIR_SUF}{key}")),
        );
    }
    Value::Object(map)
}

fn read_container(eng: &Engine, path: &str, langtype: &str) -> Value {
    let names = eng.list_kv(&format!("{path}{SEP}"));
    if langtype.starts_with("[int64]·")
        && names.iter().all(|n| {
            n.strip_prefix('[')
                .and_then(|r| r.strip_suffix(']'))
                .is_some_and(|i| i.parse::<usize>().is_ok())
        })
    {
        read_arr(eng, path)
    } else {
        read_obj(eng, path)
    }
}

fn read_obj(eng: &Engine, path: &str) -> Value {
    let mut map = Map::new();
    for name in eng.list_kv(&format!("{path}{SEP}")) {
        map.insert(name.clone(), read_value(eng, &format!("{path}{SEP}{name}")));
    }
    Value::Object(map)
}

fn read_arr(eng: &Engine, path: &str) -> Value {
    let mut idxs: Vec<usize> = Vec::new();
    for n in eng.list_kv(&format!("{path}{SEP}")) {
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

fn parse_tlv(data: &[u8]) -> (String, Vec<u8>, usize) {
    let mut h = KvspaceHead::default();
    if data.is_empty()
        || unsafe { kvspaceDecodeHead(data.as_ptr(), data.len() as u32, &mut h) } != 0
    {
        return (String::new(), Vec::new(), 1);
    }
    let kx = String::from_utf8_lossy(&h.langtype)
        .trim_end_matches('\0')
        .to_string();
    let (dims, kind) = parse_langtype(&kx);
    let (bo, bl) = (h.body_offset as usize, h.body_len.max(0) as usize);
    let raw = if bo + bl <= data.len() {
        data[bo..bo + bl].to_vec()
    } else {
        Vec::new()
    };
    let mut arr_len = 1usize;
    for d in &dims {
        arr_len *= (*d).max(1) as usize;
    }
    (kind, raw, arr_len)
}

fn tlv_to_json(kind: &str, raw: &[u8], arr_len: usize) -> Value {
    let es = elem_size(kind);
    match kind {
        "bool" => {
            if arr_len > 1 {
                Value::Array((0..arr_len).map(|i| Value::Bool(raw[i] != 0)).collect())
            } else {
                Value::Bool(raw.first().map(|&b| b != 0).unwrap_or(false))
            }
        }
        "int8" | "int16" | "int32" | "int64" | "uint8" | "uint16" | "uint32" | "uint64" => {
            if arr_len > 1 {
                Value::Array(
                    (0..arr_len)
                        .map(|i| Value::from(read_int(&raw[i * es..i * es + es])))
                        .collect(),
                )
            } else {
                Value::from(read_int(raw))
            }
        }
        "float32" | "float64" => {
            if arr_len > 1 {
                Value::Array(
                    (0..arr_len)
                        .map(|i| Value::from(float_from(&raw[i * es..i * es + es])))
                        .collect(),
                )
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

fn write_value(eng: &Engine, path: &str, v: &Value) {
    match v {
        Value::Object(m) => write_obj(eng, path, m),
        Value::Array(arr) => write_arr(eng, path, arr),
        Value::Null => eng.set_tlv(path, &tlv_encode("None", &[], &[])),
        _ => eng.set_tlv(path, &value_to_tlv(v)),
    }
}

fn write_obj(eng: &Engine, path: &str, m: &Map<String, Value>) {
    eng.set_tlv(path, &mk_obj_value());
    for k in m.keys() {
        write_value(eng, &format!("{path}{SEP}{k}"), &m[k]);
    }
}

fn write_arr(eng: &Engine, path: &str, arr: &[Value]) {
    eng.set_tlv(path, &mk_map_value());
    for (i, v) in arr.iter().enumerate() {
        write_value(eng, &format!("{path}{SEP}[{i}]"), v);
    }
}

fn value_to_tlv(v: &Value) -> Vec<u8> {
    match v {
        Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                tlv_encode("int64", &i.to_le_bytes(), &[])
            } else {
                tlv_encode("float64", &n.as_f64().unwrap_or(0.0).to_le_bytes(), &[])
            }
        }
        Value::Bool(b) => tlv_encode("bool", &[*b as u8], &[]),
        Value::String(s) => new_char_byte(s.as_bytes()),
        _ => Vec::new(),
    }
}

fn mk_obj_value() -> Vec<u8> {
    tlv_encode(OBJECT_TYPE, &[], &[])
}

fn mk_map_value() -> Vec<u8> {
    tlv_encode(ARRAY_TYPE, &[], &[])
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
    tlv_encode("char/utf32", &raw, &[utf32.len() as i32])
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::c_char;

    fn i64_tlv(vals: &[i64]) -> Vec<u8> {
        let mut raw = Vec::with_capacity(vals.len() * 8);
        for v in vals {
            raw.extend_from_slice(&v.to_le_bytes());
        }
        let d = [vals.len() as i32];
        let ds: &[i32] = if vals.len() > 1 { &d } else { &[] };
        tlv_encode("int64", &raw, ds)
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
        write_value(&eng, "/data", &v);

        let j = read_value(&eng, "/data").to_string();
        assert_eq!(
            j,
            r#"{"active":true,"age":42,"grp":{"c":1,"d":2},"items":[{"id":1,"label":"one"},{"id":2,"label":"two"}],"name":"alice","scat":[10,20,30],"score":3.14}"#
        );
    }

    #[test]
    fn native_data_to() {
        let eng = test_engine();
        eng.set_tlv("/data", &mk_obj_value());
        eng.set_tlv("/data·age", &i64_tlv(&[42]));
        eng.set_tlv("/data·name", &new_char_byte(b"alice"));
        eng.set_tlv("/data·scat", &mk_map_value());
        eng.set_tlv("/data·scat·[0]", &i64_tlv(&[10]));
        eng.set_tlv("/data·scat·[1]", &i64_tlv(&[20]));
        eng.set_tlv("/data·scat·[2]", &i64_tlv(&[30]));

        let j = read_value(&eng, "/data").to_string();
        assert_eq!(j, r#"{"age":42,"name":"alice","scat":[10,20,30]}"#);
    }
}
