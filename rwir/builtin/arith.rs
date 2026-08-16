use std::collections::HashMap;
use super::super::rwir::Param;

fn is_float(k: &str) -> bool { k.starts_with("float") }
fn is_int(k: &str) -> bool { k.starts_with("int") || k.starts_with("uint") }
fn is_str(k: &str) -> bool { k == "string" || k == "char" }

fn store_i64(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: i64) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("int64".into(), v.to_le_bytes().to_vec())); }
}
fn store_f64(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: f64) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("float64".into(), v.to_le_bytes().to_vec())); }
}
fn store_str(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], s: String) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("string".into(), s.into_bytes())); }
}

fn arith(reads: &[Param], writes: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>,
         fi: fn(i64,i64)->i64, ff: fn(f64,f64)->f64) {
    let a = &reads[0]; let b = &reads[1];
    if is_str(&a.val_kind) && is_str(&b.val_kind) {
        let sa = unsafe { String::from_utf8_lossy(a.bytes()) };
        let sb = unsafe { String::from_utf8_lossy(b.bytes()) };
        store_str(vars, writes, format!("{}{}", sa, sb));
        return;
    }
    let use_float = is_float(&a.val_kind) || is_float(&b.val_kind);
    if use_float {
        let va = if is_int(&a.val_kind) { unsafe { a.i64() as f64 } } else { unsafe { a.f64() } };
        let vb = if is_int(&b.val_kind) { unsafe { b.i64() as f64 } } else { unsafe { b.f64() } };
        store_f64(vars, writes, ff(va, vb));
    } else {
        let va = unsafe { a.i64() }; let vb = unsafe { b.i64() };
        store_i64(vars, writes, fi(va, vb));
    }
}

pub fn exec_add(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { arith(r,w,v,|a,b|a.wrapping_add(b),|a,b|a+b); }
pub fn exec_sub(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { arith(r,w,v,|a,b|a.wrapping_sub(b),|a,b|a-b); }
pub fn exec_mul(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { arith(r,w,v,|a,b|a.wrapping_mul(b),|a,b|a*b); }
pub fn exec_div(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) {
    arith(r,w,v,|a,b|{if b==0{panic!("div/0")}a.wrapping_div(b)},|a,b|{if b==0.0{panic!("div/0")}a/b});
}
pub fn exec_mod(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) {
    arith(r,w,v,|a,b|{if b==0{panic!("mod/0")}a.wrapping_rem(b)},|a,b|{if b==0.0{panic!("mod/0")}a%b});
}
pub fn exec_neg(reads: &[Param], writes: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    let p = &reads[0];
    if is_float(&p.val_kind) { store_f64(vars, writes, -(unsafe { p.f64() })); }
    else { store_i64(vars, writes, -(unsafe { p.i64() })); }
}
