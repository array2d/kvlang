use std::cmp::Ordering;
use std::collections::HashMap;
use super::super::rwir::Param;

fn store_bool(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: bool) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("bool".into(), vec![v as u8])); }
}

fn cmp_ord(a: &Param, b: &Param) -> Ordering {
    match (a.val_kind.as_str(), b.val_kind.as_str()) {
        ("string", "string") => {
            let sa = unsafe { String::from_utf8_lossy(a.bytes()) };
            let sb = unsafe { String::from_utf8_lossy(b.bytes()) };
            sa.cmp(&sb)
        }
        ("bool", "bool") => unsafe { a.first_byte().cmp(&b.first_byte()) },
        (k1, k2) if k1 == k2 => unsafe { a.i64().cmp(&b.i64()) },
        _ => {
            let va = if a.val_kind.starts_with("int") { unsafe { a.i64() as f64 } } else { unsafe { a.f64() } };
            let vb = if b.val_kind.starts_with("int") { unsafe { b.i64() as f64 } } else { unsafe { b.f64() } };
            va.partial_cmp(&vb).unwrap_or(Ordering::Equal)
        }
    }
}

fn exec_cmp(f: impl Fn(Ordering)->bool, r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) {
    store_bool(v, w, f(cmp_ord(&r[0], &r[1])));
}
pub fn exec_eq(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { exec_cmp(|o| o==Ordering::Equal, r, w, v); }
pub fn exec_neq(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { exec_cmp(|o| o!=Ordering::Equal, r, w, v); }
pub fn exec_lt(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { exec_cmp(|o| o==Ordering::Less, r, w, v); }
pub fn exec_gt(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { exec_cmp(|o| o==Ordering::Greater, r, w, v); }
pub fn exec_le(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { exec_cmp(|o| o!=Ordering::Greater, r, w, v); }
pub fn exec_ge(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { exec_cmp(|o| o!=Ordering::Less, r, w, v); }
