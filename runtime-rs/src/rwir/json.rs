//! rwir `json·to` / `json·from`：KV 子树 ↔ JSON 文本（对齐 kvlang go/json 的 · 成员形态）。
//!   json·to(root)   -> str   root 整棵子树读成 JSON：object 容器（p + memindex p·）→
//!                            嵌套对象；stringkeymap（散 key 数组 name·[i]）→ JSON 数组；
//!                            / 目录树（kind=index）→ 嵌套对象；compact ndarray → 数组。
//!   json·from(json) -> root  反序列化 JSON 写回 root 子树（覆盖语义，先 del_tree）。
//! 容器值在 p（无后缀）：object body 空、stringkeymap dims=[n]；memindex p·（kind=index，
//! body=[4B count LE][names]）是成员列表唯一权威。对象数组（choices/messages）递归支持。
//! 编码走权威 kvspace ABI：DecodeHead 读头、TlvEncode/NewCharByte 编码。

use serde_json::{Map, Value};

use crate::engine::Engine;
use crate::ffi::*;

// 常量与 kvlang go/json 的 cconst 对齐（直连 kvspace，无 kvspaceConst ABI）。
const SEP: &str = "·";
const DIR_SUF: &str = "/";
const KIND_OBJ: &str = "object";
const KIND_MAP: &str = "stringkeymap";
const KIND_INDEX: &str = "index";

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
    let s = take(unsafe { kvlang_rwirextParams(eng.kv, cs(pc).as_ptr()) });
    s.lines().map(str::to_string).collect()
}

// ── KV 子树 → JSON ────────────────────────────────────────────────

fn read_value(eng: &Engine, path: &str) -> Value {
    let (kind, raw, arr_len) = parse_tlv(&eng.get_tlv(path));
    if kind == KIND_OBJ {
        return read_obj(eng, path);
    }
    if kind == KIND_MAP {
        return read_arr(eng, path);
    }
    let dkind = parse_tlv(&eng.get_tlv(&format!("{path}{DIR_SUF}"))).0;
    if dkind == KIND_INDEX {
        return read_dir(eng, path);
    }
    if kind.is_empty() {
        return Value::Null; // None → JSON null
    }
    tlv_to_json(&kind, &raw, arr_len)
}

// read_dir：/ 目录树（kind=index）→ JSON object，子名带尾 / 先 strip。
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

// read_obj：object 容器值 p → 遍历 memindex p· 成员。
fn read_obj(eng: &Engine, path: &str) -> Value {
    let mut map = Map::new();
    for name in eng.list_kv(&format!("{path}{SEP}")) {
        map.insert(name.clone(), read_value(eng, &format!("{path}{SEP}{name}")));
    }
    Value::Object(map)
}

// read_arr：stringkeymap 容器值 p → 遍历 memindex p· 坐标段 [i]，按数值升序。
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
    let kx = String::from_utf8_lossy(&h.kindexpr)
        .trim_end_matches('\0')
        .to_string();
    let (_, dims, kind) = parse_kindexpr(&kx);
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

// ── JSON → KV 子树 ────────────────────────────────────────────────

fn write_value(eng: &Engine, path: &str, v: &Value) {
    match v {
        Value::Object(m) => write_obj(eng, path, m),
        Value::Array(arr) => write_arr(eng, path, arr),
        Value::Null => eng.set_tlv(path, &[]),
        _ => eng.set_tlv(path, &value_to_tlv(v)),
    }
}

fn write_obj(eng: &Engine, path: &str, m: &Map<String, Value>) {
    let mut keys: Vec<&String> = m.keys().collect();
    keys.sort();
    eng.set_tlv(path, &mk_obj_value());
    let names: Vec<String> = keys.iter().map(|k| k.to_string()).collect();
    eng.set_tlv(&format!("{path}{SEP}"), &mk_mem_index(&names));
    for k in keys {
        write_value(eng, &format!("{path}{SEP}{k}"), &m[k]);
    }
}

fn write_arr(eng: &Engine, path: &str, arr: &[Value]) {
    let names: Vec<String> = (0..arr.len()).map(|i| format!("[{i}]")).collect();
    eng.set_tlv(path, &mk_map_value(arr.len()));
    eng.set_tlv(&format!("{path}{SEP}"), &mk_mem_index(&names));
    for (i, v) in arr.iter().enumerate() {
        write_value(eng, &format!("{path}{SEP}[{i}]"), v);
    }
}

fn value_to_tlv(v: &Value) -> Vec<u8> {
    match v {
        Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                tlv_encode("int64", &i.to_le_bytes(), 1)
            } else {
                tlv_encode("float64", &n.as_f64().unwrap_or(0.0).to_le_bytes(), 1)
            }
        }
        Value::Bool(b) => tlv_encode("bool", &[*b as u8], 1),
        Value::String(s) => new_char_byte(s.as_bytes()),
        _ => Vec::new(),
    }
}

// ── 容器值 / memindex 编码 ─────────────────────────────────────────

// memindex p·：kind=index，body=[4B count LE][names]。
fn mk_mem_index(names: &[String]) -> Vec<u8> {
    let mut body = (names.len() as u32).to_le_bytes().to_vec();
    body.extend_from_slice(names.join("\n").as_bytes());
    tlv_encode(KIND_INDEX, &body, 1)
}

// object 容器值 p：body 空。
fn mk_obj_value() -> Vec<u8> {
    tlv_encode(KIND_OBJ, &[], 1)
}

// stringkeymap 容器值 p：body 空，dims=[n]（恒一维坐标段）。
fn mk_map_value(n: usize) -> Vec<u8> {
    tlv_encode_dims(KIND_MAP, &[], &[n as i32])
}

// ── TLV 编码（权威 kvspace ABI）───────────────────────────────────

fn tlv_encode(kind: &str, raw: &[u8], arr_len: usize) -> Vec<u8> {
    let dims = [arr_len as i32];
    let ds: &[i32] = if arr_len > 1 { &dims } else { &[] };
    tlv_encode_dims(kind, raw, ds)
}

fn tlv_encode_dims(kind: &str, raw: &[u8], dims: &[i32]) -> Vec<u8> {
    unsafe {
        let (mut out, mut olen) = (std::ptr::null_mut(), 0u32);
        kvspaceTlvEncode(
            cs(kind).as_ptr(),
            raw.as_ptr(),
            raw.len() as u32,
            if dims.is_empty() {
                std::ptr::null()
            } else {
                dims.as_ptr()
            },
            dims.len() as i32,
            &mut out,
            &mut olen,
        );
        if out.is_null() || olen == 0 {
            return Vec::new();
        }
        let v = std::slice::from_raw_parts(out, olen as usize).to_vec();
        libc::free(out as *mut std::ffi::c_void);
        v
    }
}

fn new_char_byte(bytes: &[u8]) -> Vec<u8> {
    let utf32: Vec<u32> = String::from_utf8_lossy(bytes)
        .chars()
        .map(|c| c as u32)
        .collect();
    let raw: Vec<u8> = utf32.iter().flat_map(|v| v.to_le_bytes()).collect();
    tlv_encode_dims("char/utf32", &raw, &[utf32.len() as i32])
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
        tlv_encode("int64", &raw, vals.len())
    }

    fn test_engine() -> Engine {
        let dsn = "redis://127.0.0.1:6379";
        let kv = unsafe { kvspaceConnect(cs(dsn).as_ptr()) };
        assert!(!kv.is_null());
        let mut err = [0u8; 256];
        unsafe { kvspaceClear(kv, err.as_mut_ptr() as *mut c_char, 256) };
        Engine {
            rt: std::ptr::null_mut(),
            kv,
            dsn: dsn.to_string(),
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
        // 用户 kv 代码写 · 成员（object 容器 + memindex），json·to 读回。
        eng.set_tlv("/data", &mk_obj_value());
        eng.set_tlv(
            "/data·",
            &mk_mem_index(&["age".into(), "name".into(), "scat".into()]),
        );
        eng.set_tlv("/data·age", &i64_tlv(&[42]));
        eng.set_tlv("/data·name", &new_char_byte(b"alice"));
        eng.set_tlv("/data·scat", &mk_map_value(3));
        eng.set_tlv(
            "/data·scat·",
            &mk_mem_index(&["[0]".into(), "[1]".into(), "[2]".into()]),
        );
        eng.set_tlv("/data·scat·[0]", &i64_tlv(&[10]));
        eng.set_tlv("/data·scat·[1]", &i64_tlv(&[20]));
        eng.set_tlv("/data·scat·[2]", &i64_tlv(&[30]));

        let j = read_value(&eng, "/data").to_string();
        assert_eq!(j, r#"{"age":42,"name":"alice","scat":[10,20,30]}"#);
    }
}
