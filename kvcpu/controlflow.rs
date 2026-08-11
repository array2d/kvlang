//! Control flow — matching kvcpu/controlflow.go.
use super::cpu::KVCpu;
use crate::rwir::rwir::Rwir;
use crate::keytree::r#const::lib_path;

pub fn handle_call(cpu: &KVCpu, _pc: &str, inst: &Rwir) -> Result<(), String> {
    if inst.reads.is_empty() { return Err("call needs func name".into()); }
    let func_name = &inst.reads[0].name;
    let callpc_key = format!("{}.callpc", lib_path(func_name));
    if let Some(rv) = cpu.get(&callpc_key) {
        let _entry = unsafe { String::from_utf8_lossy(rv.bytes()).to_string() };
        super::execute::execute(cpu, func_name)?;
        return Ok(());
    }
    Err(format!("call: func {} not found", func_name))
}

pub fn handle_return(_cpu: &KVCpu, _pc: &str) -> Result<(), String> {
    Ok(()) // simplified: return just exits current scope
}

pub fn handle_br(cpu: &KVCpu, pc: &str, inst: &Rwir,
                  vars: &mut std::collections::HashMap<String, (String, Vec<u8>)>) -> Result<(), String> {
    if inst.reads.len() < 3 { return Err("br needs 3 args".into()); }
    let cond = unsafe { inst.reads[0].bool() };
    let label_idx = if cond { 1 } else { 2 };
    handle_goto(cpu, pc, &Rwir {
        opcode: "goto".into(),
        reads: vec![inst.reads[label_idx].clone()],
        writes: vec![],
    }, vars)
}

/// goto(label): Execute label scope as nested function under same function root.
/// Go equivalent: HandleScope creates new frame at rwRoot/scopeName/.
pub fn handle_goto(cpu: &KVCpu, pc: &str, inst: &Rwir,
                    vars: &mut std::collections::HashMap<String, (String, Vec<u8>)>) -> Result<(), String> {
    if inst.reads.is_empty() { return Err("goto needs label".into()); }
    let label = unsafe { String::from_utf8_lossy(inst.reads[0].bytes()).to_string() };
    let scope_key = format!("{}/{}", pc, label);
    if cpu.get(&format!("{}/[0,0]", scope_key)).is_some() {
        let mut slot: i32 = 0;
        loop {
            let mut inst = crate::rwir::rwir::decode(cpu, &scope_key, slot)
                .ok_or_else(|| format!("goto: decode failed at {}", slot))?;
            if inst.opcode.is_empty() { break; }
            for p in &mut inst.reads {
                if p.val_kind == "rwir" {
                    if let Some((k, v)) = vars.get(&p.name) {
                        p.val_kind = k.clone(); p.body_ptr = v.as_ptr(); p.body_len = v.len() as i32;
                    }
                }
            }
            let op = inst.opcode.clone();
            if !crate::rwir::builtin::ops::native(cpu, &op, &inst.reads, &inst.writes, vars) {
                return Err(format!("goto: unknown op in scope: {}", op));
            }
            slot += 1;
        }
    }
    Ok(())
}
