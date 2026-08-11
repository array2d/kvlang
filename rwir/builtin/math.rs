use std::collections::HashMap;
use super::super::rwir::Param;
fn f64_arg(p: &Param) -> f64 {
    if p.val_kind.starts_with("int") || p.val_kind.starts_with("uint") { unsafe { p.i64() as f64 } }
    else { unsafe { p.f64() } }
}
fn store(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: f64) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("float64".into(), v.to_le_bytes().to_vec())); }
}
fn store_i64(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: i64) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("int64".into(), v.to_le_bytes().to_vec())); }
}
pub fn exec_pow(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, f64_arg(&r[0]).powf(f64_arg(&r[1]))); }
pub fn exec_sqrt(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, f64_arg(&r[0]).sqrt()); }
pub fn exec_abs(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { let x = unsafe { r[0].i64() }; store_i64(v, w, if x<0 {-x} else {x}); }
pub fn exec_exp(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, f64_arg(&r[0]).exp()); }
pub fn exec_log(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, f64_arg(&r[0]).ln()); }
