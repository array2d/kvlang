use crate::kvcpu::cpu::{KVCpu, RawValue};

const MAX_PARAMS: i32 = 128;

/// Param: name is copied (small), body points directly into SHM (zero-copy).
#[derive(Clone)]
pub struct Param {
    pub name: String,
    pub val_kind: String,
    pub body_ptr: *const u8,
    pub body_len: i32,
}

pub struct Rwir {
    pub opcode: String,
    pub reads: Vec<Param>,
    pub writes: Vec<Param>,
}

impl Param {
    /// reinterpret_cast body_ptr → i64 (LE, unaligned OK on x86_64)
    pub unsafe fn i64(&self) -> i64 { (self.body_ptr as *const i64).read_unaligned() }
    /// reinterpret_cast body_ptr → f64 (LE, unaligned OK on x86_64)
    pub unsafe fn f64(&self) -> f64 { (self.body_ptr as *const f64).read_unaligned() }
    /// reinterpret_cast body_ptr → u32 (LE)
    pub unsafe fn u32(&self) -> u32 { (self.body_ptr as *const u32).read_unaligned() }
    /// pointer to body bytes as slice, zero-copy
    pub unsafe fn bytes(&self) -> &[u8] { std::slice::from_raw_parts(self.body_ptr, self.body_len as usize) }
    /// first byte
    pub unsafe fn first_byte(&self) -> u8 { *self.body_ptr }
    /// bool
    pub unsafe fn bool(&self) -> bool { *self.body_ptr != 0 }
}

/// Build Param from RawValue. If kind is "rwir", strip 4-byte header for name.
fn make_param(rv: &RawValue) -> Param {
    let name = if rv.kind == "rwir" && rv.body_len >= 4 {
        let n = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
        String::from_utf8_lossy(&n[4..]).to_string()
    } else {
        let n = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
        String::from_utf8_lossy(n).to_string()
    };
    Param { name, val_kind: rv.kind.clone(), body_ptr: rv.body_ptr, body_len: rv.body_len }
}

/// Decode instruction at slot N under func_base (e.g., "/lib/main/[N,0]").
pub fn decode(cpu: &KVCpu, func_base: &str, slot: i32) -> Option<Rwir> {
    if func_base.is_empty() { return None; }
    let base = format!("{}/[{}", func_base, slot);
    let mut r = Rwir { opcode: String::new(), reads: Vec::new(), writes: Vec::new() };

    // opcode: [slot, 0]
    if let Some(rv) = cpu.get(&format!("{},0]", base)) {
        let raw = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
        r.opcode = String::from_utf8_lossy(&raw[4.min(raw.len())..]).to_string();
    }

    for i in 1..=MAX_PARAMS {
        if let Some(rv) = cpu.get(&format!("{},-{}]", base, i)) {
            r.reads.push(make_param(&rv));
            if i == MAX_PARAMS { return None; }
        }
        if let Some(rv) = cpu.get(&format!("{},{}]", base, i)) {
            r.writes.push(make_param(&rv));
            if i == MAX_PARAMS { return None; }
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
