use std::collections::HashMap;
use super::super::rwir::Param;
pub fn exec_cast(r: &[Param], w: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>) {
    if let (Some(src), Some(dst)) = (r.first(), w.first()) {
        let raw = unsafe { src.bytes().to_vec() };
        vars.insert(dst.name.clone(), (src.val_kind.clone(), raw));
    }
}
