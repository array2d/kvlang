use std::collections::HashMap;
use super::super::rwir::Param;
fn store(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: i64) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("int64".into(), v.to_le_bytes().to_vec())); }
}
pub fn exec_bitand(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64() & r[1].i64() }); }
pub fn exec_bitor(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64() | r[1].i64() }); }
pub fn exec_bitxor(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64() ^ r[1].i64() }); }
pub fn exec_shl(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64() << r[1].i64() }); }
pub fn exec_shr(r: &[Param], w: &[Param], v: &mut HashMap<String, (String, Vec<u8>)>) { store(v, w, unsafe { r[0].i64() >> r[1].i64() }); }
