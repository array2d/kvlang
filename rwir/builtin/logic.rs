use std::collections::HashMap;
use super::super::rwir::Param;
fn store(vars: &mut HashMap<String, (String, Vec<u8>)>, w: &[Param], v: bool) {
    if let Some(w) = w.first() { vars.insert(w.name.clone(), ("bool".into(), vec![v as u8])); }
}
pub fn exec_not(r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) { store(vars, w, unsafe { r[0].first_byte() == 0 }); }
pub fn exec_and(r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) { store(vars, w, unsafe { r[0].first_byte() != 0 && r[1].first_byte() != 0 }); }
pub fn exec_or(r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) { store(vars, w, unsafe { r[0].first_byte() != 0 || r[1].first_byte() != 0 }); }
