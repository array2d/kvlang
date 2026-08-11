use std::collections::HashMap;
use super::cpu::KVCpu;
use crate::rwir::rwir::{Rwir, Param, decode};
use crate::rwir::builtin::ops;
use crate::keytree::r#const::lib_path;

/// execute runs a function from kvspace SHM. func_name is like "main".
pub fn execute(cpu: &KVCpu, func_name: &str) -> Result<(), String> {
    let func_base = lib_path(func_name);
    let mut vars: HashMap<String, (String, Vec<u8>)> = HashMap::new();
    let mut slot: i32 = 0;

    loop {
        let mut inst = decode(cpu, &func_base, slot).ok_or_else(|| format!("decode failed at slot {}", slot))?;
        if inst.opcode.is_empty() { break; }
        // Resolve variable references
        for p in &mut inst.reads {
            if p.val_kind == "rwir" {
                if let Some((k, v)) = vars.get(&p.name) {
                    p.val_kind = k.clone();
                    p.body_ptr = v.as_ptr();
                    p.body_len = v.len() as i32;
                }
            }
        }

        // Dispatch: builtin ops via ops::native, then controlflow, then user func call
        let op = inst.opcode.clone();
        if ops::native(cpu, &op, &inst.reads, &inst.writes, &mut vars) {
            // handled by builtin
        } else if matches!(op.as_str(), "call" | "goto" | "br" | "return") {
            // control flow — delegated to controlflow (stub for now)
            return Err(format!("control flow not yet: {}", op));
        } else {
            // User-defined function → recursive call
            let fk = lib_path(&op);
            if cpu.get(&fk).is_some() {
                execute(cpu, &op)?;
            } else {
                return Err(format!("unknown op: {}", op));
            }
        }
        slot += 1;
    }
    Ok(())
}
