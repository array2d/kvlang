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

    // Check function exists
    if cpu.get(&format!("{}/[0,0]", func_base)).is_none() {
        return Err(format!("func not found: {}", func_name));
    }
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
            let result = match op.as_str() {
                "call" => super::controlflow::handle_call(cpu, &func_base, &inst),
                "goto" => super::controlflow::handle_goto(cpu, &func_base, &inst, &mut vars),
                "br" => super::controlflow::handle_br(cpu, &func_base, &inst, &mut vars),
                "return" => super::controlflow::handle_return(cpu, &func_base),
                _ => unreachable!(),
            };
            result?;
        } else {
            // User-defined function → recursive call
            let fname = if op.starts_with("/lib/") { op[5..].to_string() } else { op.clone() };
            let fk = lib_path(&fname);
            if cpu.get(&fk).is_some() {
                execute(cpu, &fname)?;
            } else {
                return Err(format!("unknown op: {}", op));
            }
        }
        slot += 1;
    }
    Ok(())
}
