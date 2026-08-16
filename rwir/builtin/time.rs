use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};
use super::super::rwir::Param;

fn store(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: i64) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("int64".into(), v.to_le_bytes().to_vec())); }
}
fn store_bool(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: bool) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("bool".into(), vec![v as u8])); }
}

pub fn exec_time_now(_r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos() as i64;
    store(vars, w, now);
}
pub fn exec_time_sub(r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    store(vars, w, unsafe { r[0].i64().wrapping_sub(r[1].i64()) });
}
pub fn exec_time_add(r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    store(vars, w, unsafe { r[0].i64().wrapping_add(r[1].i64()) });
}
pub fn exec_time_before(r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    store_bool(vars, w, unsafe { r[0].i64() < r[1].i64() });
}
pub fn exec_time_after(r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    store_bool(vars, w, unsafe { r[0].i64() > r[1].i64() });
}
pub fn exec_duration_nanos(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64() }); }
pub fn exec_duration_millis(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64().wrapping_mul(1_000_000) }); }
pub fn exec_duration_seconds(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64().wrapping_mul(1_000_000_000) }); }
pub fn exec_duration_as_nanos(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64() }); }
pub fn exec_duration_as_millis(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64() / 1_000_000 }); }
pub fn exec_duration_before(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store_bool(v, w, unsafe { r[0].i64() < r[1].i64() }); }
pub fn exec_duration_after(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store_bool(v, w, unsafe { r[0].i64() > r[1].i64() }); }
pub fn exec_duration_as_seconds(r: &[super::super::rwir::Param], w: &[super::super::rwir::Param], vars: &mut std::collections::HashMap<String, (String, Vec<u8>)>) {
    store(vars, w, unsafe { r[0].i64() / 1_000_000_000 });
}
