use std::collections::HashMap;
use super::super::rwir::Param;
fn store(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], s: &str) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("string".into(), s.as_bytes().to_vec())); }
}
fn store_i64(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: i64) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("int64".into(), v.to_le_bytes().to_vec())); }
}
pub fn exec_at(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) {
    let s = unsafe { String::from_utf8_lossy(r[0].bytes()) };
    let idx = unsafe { r[1].i64() as usize };
    if let Some(c) = s.chars().nth(idx) { store(v, w, &c.to_string()); }
}
pub fn exec_len(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) {
    store_i64(v, w, unsafe { String::from_utf8_lossy(r[0].bytes()).chars().count() as i64 });
}
pub fn exec_concat(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) {
    let s: String = r.iter().map(|p| unsafe { String::from_utf8_lossy(p.bytes()).to_string() }).collect();
    store(v, w, &s);
}
