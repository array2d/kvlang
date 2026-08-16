use std::collections::HashMap;
use super::cpu::KVCpu;
use crate::rwir::rwir::{Rwir, Param, decode};
use crate::rwir::builtin::ops;
use crate::keytree::r#const;

/// SlotValue references SHM directly — zero-copy.
/// kind is copied (small), body_ptr points into SHM.
#[derive(Clone)]
pub struct SlotValue {
    pub kind: String,
    pub body_ptr: *const u8,
    pub body_len: i32,
}

/// Execute a function from Go-layout kvspace SHM.
pub fn execute(cpu: &KVCpu, func_name: &str) -> Result<(), String> {
    let func_dir = parse_func_dir(func_name);
    execute_inner(cpu, &func_dir, &[], &HashMap::new(), &[])?;
    Ok(())
}

pub fn parse_func_dir(name: &str) -> String {
    let (pkg, fname) = if let Some(dot) = name.rfind('.') {
        (&name[..dot], &name[dot+1..])
    } else {
        ("", name)
    };
    r#const::lib_func_dir(pkg, fname)
}

/// Execute with pre-resolved args. Returns the callee's local vars (for return values).
pub fn execute_inner(
    cpu: &KVCpu,
    func_dir: &str,
    _arg_names: &[String],
    caller_args: &HashMap<String, SlotValue>,
    return_names: &[String],       // ordered write-param names to return
) -> Result<HashMap<String, (String, Vec<u8>)>, String> {
    let sig_rv = cpu.get(&format!("{}/[0,0]", func_dir))
        .ok_or_else(|| format!("func not found: {}", func_dir))?;
    if sig_rv.kind != r#const::KIND_RWFUNC {
        return Err(format!("{} has no rwfunc at [0,0]", func_dir));
    }
    let nr = unsafe { sig_rv.u16_le(0) } as i32;
    let nw = unsafe { sig_rv.u16_le(2) } as i32;

    let name_slot = build_name_slot_map(cpu, func_dir);

    let mut slot_vals: HashMap<String, SlotValue> = HashMap::new();
    for (slot, val) in caller_args {
        slot_vals.insert(slot.clone(), val.clone());
    }

    let mut vars: HashMap<String, (String, Vec<u8>)> = HashMap::new();

    let mut slot_idx: i32 = 1;
    loop {
        let inst = decode(cpu, func_dir, slot_idx, 128, 128)
            .ok_or_else(|| format!("decode failed at slot {}", slot_idx))?;
        if inst.opcode.is_empty() { break; }

        let mut resolved_inst = inst.clone();
        for p in &mut resolved_inst.reads {
            if p.val_kind == r#const::KIND_RWIR {
                let sv = resolve_read(cpu, &name_slot, &slot_vals, &vars, p);
                if sv.body_ptr.is_null() { continue; }
                p.val_kind = sv.kind;
                p.body_ptr = sv.body_ptr;
                p.body_len = sv.body_len;
            }
        }

        let op = resolved_inst.opcode.clone();
        if ops::native(cpu, &op, &resolved_inst.reads, &resolved_inst.writes, &mut vars) {
            // handled by builtin
        } else if matches!(op.as_str(), "call" | "goto" | "br" | "return") {
            match op.as_str() {
                "call" => super::controlflow::handle_call(cpu, func_dir, &resolved_inst, &name_slot, &slot_vals, &mut vars)?,
                "return" => break, // exit loop, return vars
                "goto" => super::controlflow::handle_goto(cpu, func_dir, &resolved_inst, &name_slot, &slot_vals, &mut vars)?,
                "br" => super::controlflow::handle_br(cpu, func_dir, &resolved_inst, &name_slot, &slot_vals, &mut vars)?,
                _ => {}
            };
        } else {
            let callee_dir = parse_func_dir(&op);
            let callee_returns = build_return_names(cpu, &callee_dir);
            let mut args: HashMap<String, SlotValue> = HashMap::new();
            for (i, read) in resolved_inst.reads.iter().enumerate() {
                args.insert(format!("[0,-{}]", i+1), SlotValue {
                    kind: read.val_kind.clone(),
                    body_ptr: read.body_ptr,
                    body_len: read.body_len,
                });
            }
            let ret_vars = execute_inner(cpu, &callee_dir, &[], &args, &callee_returns)?;
            for (i, ret_name) in callee_returns.iter().enumerate() {
                if let Some((k, v)) = ret_vars.get(ret_name) {
                    if let Some(w) = resolved_inst.writes.get(i) {
                        vars.insert(w.name.clone(), (k.clone(), v.clone()));
                    }
                }
            }
        }
        slot_idx += 1;
    }

    // Collect return values
    let mut ret = HashMap::new();
    for name in return_names {
        if let Some(v) = vars.get(name) {
            ret.insert(name.clone(), v.clone());
        }
    }
    Ok(ret)
}

pub fn build_name_slot_map(cpu: &KVCpu, func_dir: &str) -> HashMap<String, String> {
    let mut map = HashMap::new();
    let children = cpu.list(&format!("{}/", func_dir));
    for child in children {
        if child.starts_with('[') || child.starts_with('.') { continue; }
        if let Some(rv) = cpu.get(&format!("{}/{}", func_dir, child)) {
            if rv.is_ptr {
                let target = unsafe { rv.ptr_target() }.to_string();
                map.insert(child, target);
            }
        }
    }
    map
}

pub fn build_param_names(cpu: &KVCpu, func_dir: &str) -> Vec<String> {
    let name_slot = build_name_slot_map(cpu, func_dir);
    let mut pairs: Vec<(i32, String)> = name_slot.iter()
        .filter_map(|(name, slot)| {
            if slot.starts_with("[0,-") {
                let idx: i32 = slot.trim_start_matches("[0,-").trim_end_matches(']').parse().ok()?;
                Some((idx, name.clone()))
            } else { None }
        })
        .collect();
    pairs.sort_by_key(|(idx, _)| *idx);
    pairs.into_iter().map(|(_, name)| name).collect()
}

pub fn build_return_names(cpu: &KVCpu, func_dir: &str) -> Vec<String> {
    let name_slot = build_name_slot_map(cpu, func_dir);
    let mut pairs: Vec<(i32, String)> = name_slot.iter()
        .filter_map(|(name, slot)| {
            if slot.starts_with("[0,") && !slot.starts_with("[0,-") {
                let idx: i32 = slot.trim_start_matches("[0,").trim_end_matches(']').parse().ok()?;
                Some((idx, name.clone()))
            } else { None }
        })
        .collect();
    pairs.sort_by_key(|(idx, _)| *idx);
    pairs.into_iter().map(|(_, name)| name).collect()
}

pub fn resolve_read(
    _cpu: &KVCpu,
    name_slot: &HashMap<String, String>,
    slot_vals: &HashMap<String, SlotValue>,
    vars: &HashMap<String, (String, Vec<u8>)>,
    p: &Param,
) -> SlotValue {
    if p.val_kind != r#const::KIND_RWIR && p.val_kind != r#const::KIND_RWFUNC {
        return SlotValue { kind: p.val_kind.clone(), body_ptr: p.body_ptr, body_len: p.body_len };
    }
    let name = &p.name;
    if name.is_empty() { return SlotValue { kind: r#const::KIND_NONE.to_string(), body_ptr: std::ptr::null(), body_len: 0 }; }
    if let Some(slot) = name_slot.get(name) {
        if let Some(sv) = slot_vals.get(slot) {
            return sv.clone();
        }
    }
    if let Some((k, v)) = vars.get(name) {
        return SlotValue { kind: k.clone(), body_ptr: v.as_ptr(), body_len: v.len() as i32 };
    }
    SlotValue { kind: r#const::KIND_NONE.to_string(), body_ptr: std::ptr::null(), body_len: 0 }
}
