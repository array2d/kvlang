use std::collections::HashMap;
use crate::kvcpu::cpu::KVCpu;
use super::super::rwir::Param;
pub fn exec_set(_cpu: &KVCpu, r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    if let (Some(src), Some(dst)) = (r.first(), w.first()) {
        let raw = unsafe { src.bytes().to_vec() };
        vars.insert(dst.name.clone(), (src.val_kind.clone(), raw));
    }
}
pub fn exec_kvhas(cpu: &KVCpu, r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    if let Some(p) = r.first() {
        let key = unsafe { String::from_utf8_lossy(p.bytes()).to_string() };
        let exists = cpu.get(&key).is_some();
        if let Some(w) = w.first() { vars.insert(w.name.clone(), ("bool".into(), vec![exists as u8])); }
    }
}
