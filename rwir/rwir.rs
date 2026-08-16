use crate::kvcpu::cpu::{KVCpu, RawValue};

/// Param: name is copied (small, required for HashMap lookup), body points directly into SHM.
#[derive(Clone)]
pub struct Param {
    pub name: String,
    pub val_kind: String,
    pub body_ptr: *const u8,
    pub body_len: i32,
}

#[derive(Clone)]
pub struct Rwir {
    pub opcode: String,
    pub reads: Vec<Param>,
    pub writes: Vec<Param>,
}

impl Param {
    /// reinterpret_cast body_ptr → i64 (LE, unaligned OK on x86_64)
    pub unsafe fn i64(&self) -> i64 { (self.body_ptr as *const i64).read_unaligned() }
    /// reinterpret_cast body_ptr → f64 (LE)
    pub unsafe fn f64(&self) -> f64 { (self.body_ptr as *const f64).read_unaligned() }
    /// reinterpret_cast body_ptr → i32 (LE)
    pub unsafe fn i32(&self) -> i32 { (self.body_ptr as *const i32).read_unaligned() }
    /// pointer to body bytes as slice, zero-copy
    pub unsafe fn bytes(&self) -> &[u8] { std::slice::from_raw_parts(self.body_ptr, self.body_len as usize) }
    /// first byte
    pub unsafe fn first_byte(&self) -> u8 { *self.body_ptr }
    /// bool
    pub unsafe fn bool(&self) -> bool { *self.body_ptr != 0 }
}

/// Build Param from RawValue.
/// For rwir-kind slots: strip 4-byte header ([nr|nw]) → body[4..] = name.
/// For concrete kinds: body is the value bytes.
fn make_param(rv: &RawValue) -> Param {
    let name = if rv.kind == crate::keytree::r#const::KIND_RWIR && rv.body_len >= 4 {
        let n = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
        String::from_utf8_lossy(&n[4..]).to_string()
    } else {
        let n = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
        String::from_utf8_lossy(n).to_string()
    };
    Param { name, val_kind: rv.kind.clone(), body_ptr: rv.body_ptr, body_len: rv.body_len }
}

/// Decode instruction at slot N under func_base.
/// max_reads/max_writes bound the slot scan (from rwfunc for slot 0, 128 for instructions).
pub fn decode(cpu: &KVCpu, func_base: &str, slot: i32, max_reads: i32, max_writes: i32) -> Option<Rwir> {
    if func_base.is_empty() { return None; }
    let base = format!("{}/[{}", func_base, slot);
    let mut r = Rwir { opcode: String::new(), reads: Vec::new(), writes: Vec::new() };

    // opcode: [slot, 0]
    if let Some(rv) = cpu.get(&format!("{},0]", base)) {
        let raw = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
        let skip = if rv.kind == crate::keytree::r#const::KIND_RWIR && raw.len() >= 4 { 4 } else { 0 };
        r.opcode = String::from_utf8_lossy(&raw[skip..]).to_string();
    }

    let nr = max_reads.min(128);
    let nw = max_writes.min(128);

    for i in 1..=nr {
        if let Some(rv) = cpu.get(&format!("{},-{}]", base, i)) {
            r.reads.push(make_param(&rv));
        }
    }
    for i in 1..=nw {
        if let Some(rv) = cpu.get(&format!("{},{}]", base, i)) {
            r.writes.push(make_param(&rv));
        }
    }
    Some(r)
}

pub fn next_pc(pc: &str) -> String {
    if let Some(lb) = pc.rfind('[') {
        if let Some(rb) = pc[lb..].find(']') {
            let inner = &pc[lb+1..lb+rb];
            if let Some(comma) = inner.find(',') {
                let off: i32 = inner[comma+1..].parse().unwrap_or(0);
                return format!("{}[{},{}]", &pc[..lb], &inner[..comma], off + 1);
            }
        }
    }
    pc.to_string()
}
