use super::super::rwir::Param;
pub fn display(kind: &str, ptr: *const u8, len: i32) -> String {
    if kind == "string" || kind == "char" { return unsafe { String::from_utf8_lossy(std::slice::from_raw_parts(ptr, len as usize)).to_string() }; }
    if kind == "bool" { return unsafe { if *ptr != 0 { "true".into() } else { "false".into() } }; }
    if kind.starts_with("int") || kind.starts_with("uint") { return unsafe { (ptr as *const i64).read_unaligned().to_string() }; }
    if kind.starts_with("float") { let v = unsafe { (ptr as *const f64).read_unaligned() }; return if v == v.floor() && v.is_finite() { format!("{:.1}", v) } else { v.to_string() }; }
    unsafe { String::from_utf8_lossy(std::slice::from_raw_parts(ptr, len as usize)).to_string() }
}
pub fn println_op(reads: &[Param]) {
    let parts: Vec<String> = reads.iter().map(|p| display(&p.val_kind, p.body_ptr, p.body_len)).collect();
    println!("{}", parts.join(" "));
}
