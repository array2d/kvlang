//! Control flow — matching kvcpu/controlflow.go.
use std::collections::HashMap;
use super::cpu::KVCpu;
use crate::rwir::rwir::{Rwir, decode};
use crate::rwir::builtin::ops;
use crate::keytree::r#const;

use super::execute::{SlotValue, parse_func_dir, execute_inner, build_name_slot_map, build_param_names, build_return_names, resolve_read};

/// handle_call: read callee params, pass args, execute, propagate return values.
pub fn handle_call(
    cpu: &KVCpu,
    _caller_dir: &str,
    inst: &Rwir,
    caller_name_slot: &HashMap<String, String>,
    caller_slot_vals: &HashMap<String, SlotValue>,
    vars: &mut HashMap<String, (String, Vec<u8>)>,
) -> Result<(), String> {
    if inst.reads.is_empty() { return Err("call needs func name".into()); }
    let func_name = &inst.reads[0].name;

    let callee_dir = if func_name.starts_with("/lib/") {
        func_name["/lib/".len()..].to_string()
    } else {
        func_name.clone()
    };
    let callee_dir = parse_func_dir(&callee_dir);

    let sig_rv = cpu.get(&format!("{}/[0,0]", callee_dir))
        .ok_or_else(|| format!("call: func not found: {}", func_name))?;
    if sig_rv.kind != r#const::KIND_RWFUNC {
        return Err(format!("call: {} has no rwfunc", func_name));
    }
    let nr = unsafe { sig_rv.u16_le(0) } as usize;

    let callee_names = build_param_names(cpu, &callee_dir);

    // Build args by position
    let mut args: HashMap<String, SlotValue> = HashMap::new();
    for i in 0..nr {
        let slot = format!("[0,-{}]", i + 1);
        if let Some(arg) = inst.reads.get(i) {
            let sv = resolve_read(cpu, caller_name_slot, caller_slot_vals, vars, arg);
            args.insert(slot, sv);
        }
    }

    let callee_returns = build_return_names(cpu, &callee_dir);
    let ret = execute_inner(cpu, &callee_dir, &callee_names, &args, &callee_returns)?;
    for (i, ret_name) in callee_returns.iter().enumerate() {
        if let Some((k, v)) = ret.get(ret_name) {
            if let Some(w) = inst.writes.get(i) {
                vars.insert(w.name.clone(), (k.clone(), v.clone()));
            }
        }
    }
    Ok(())
}

pub fn handle_return() -> Result<(), String> { Ok(()) }

/// handle_br(cond, trueLabel, falseLabel): evaluate resolved cond → goto label.
pub fn handle_br(
    cpu: &KVCpu, func_dir: &str, inst: &Rwir,
    name_slot: &HashMap<String, String>,
    slot_vals: &HashMap<String, SlotValue>,
    vars: &mut HashMap<String, (String, Vec<u8>)>,
) -> Result<(), String> {
    if inst.reads.len() < 3 { return Err("br needs 3 args".into()); }
    let cond = unsafe { inst.reads[0].first_byte() != 0 };
    let label_idx = if cond { 1 } else { 2 };
    let label_name = unsafe {
        String::from_utf8_lossy(inst.reads[label_idx].bytes()).to_string()
    };
    execute_scope(cpu, func_dir, &label_name, name_slot, slot_vals, vars)
}

/// handle_goto(label): execute label scope.
pub fn handle_goto(
    cpu: &KVCpu, func_dir: &str, inst: &Rwir,
    name_slot: &HashMap<String, String>,
    slot_vals: &HashMap<String, SlotValue>,
    vars: &mut HashMap<String, (String, Vec<u8>)>,
) -> Result<(), String> {
    if inst.reads.is_empty() { return Err("goto needs label".into()); }
    let label = unsafe { String::from_utf8_lossy(inst.reads[0].bytes()).to_string() };
    execute_scope(cpu, func_dir, &label, name_slot, slot_vals, vars)
}

/// execute_scope: run instructions under func_dir/label[0,0], [1,0], ...
/// Supports br within scopes for while loops.
fn execute_scope(
    cpu: &KVCpu, func_dir: &str, label: &str,
    name_slot: &HashMap<String, String>,
    slot_vals: &HashMap<String, SlotValue>,
    vars: &mut HashMap<String, (String, Vec<u8>)>,
) -> Result<(), String> {
    // Go layout: func_dir/label[0,0], func_dir/label[1,0], ...
    let scope_prefix = format!("{}/{}", func_dir, label);
    let scope_base = if cpu.get(&format!("{}[0,0]", scope_prefix)).is_some() {
        scope_prefix
    } else {
        format!("{}{}", func_dir, label)
    };

    // Scope keys: func_dir/label[0,0], func_dir/label[1,0] — no "/" before "["
    let mut slot: i32 = 0;
    loop {
        let mut inst = decode_scope(cpu, &scope_base, slot)
            .ok_or_else(|| format!("scope decode failed at {}", slot))?;
        if inst.opcode.is_empty() { break; }

        // Resolve variable references in-place
        for p in &mut inst.reads {
            if p.val_kind == r#const::KIND_RWIR {
                let sv = resolve_read(cpu, name_slot, slot_vals, vars, p);
                if sv.body_ptr.is_null() { continue; }
                p.val_kind = sv.kind;
                p.body_ptr = sv.body_ptr;
                p.body_len = sv.body_len;
            }
        }

        let op = inst.opcode.clone();
        if ops::native(cpu, &op, &inst.reads, &inst.writes, vars) {
            // handled by builtin
        } else if op == "br" {
            // Conditional branch within scope → jump to different scope
            if inst.reads.len() < 3 { continue; }
            let cond = unsafe { inst.reads[0].first_byte() != 0 };
            let label_idx = if cond { 1 } else { 2 };
            let next_label = unsafe { String::from_utf8_lossy(inst.reads[label_idx].bytes()).to_string() };
            return execute_scope(cpu, func_dir, &next_label, name_slot, slot_vals, vars);
        } else if op == "goto" {
            if let Some(p) = inst.reads.first() {
                let next_label = unsafe { String::from_utf8_lossy(p.bytes()).to_string() };
                return execute_scope(cpu, func_dir, &next_label, name_slot, slot_vals, vars);
            }
        }
        // Other opcodes in scope (like "return") — skip
        slot += 1;
    }
    Ok(())
}

/// Decode scope instruction: scope_base[slot,0], scope_base[slot,-j], scope_base[slot,+j].
fn decode_scope(cpu: &KVCpu, scope_base: &str, slot: i32) -> Option<Rwir> {
    use crate::rwir::rwir::Param;
    let base = format!("{}[{}", scope_base, slot);
    let mut r = Rwir { opcode: String::new(), reads: Vec::new(), writes: Vec::new() };
    if let Some(rv) = cpu.get(&format!("{},0]", base)) {
        let raw = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
        let skip = if rv.kind == r#const::KIND_RWIR && raw.len() >= 4 { 4 } else { 0 };
        r.opcode = String::from_utf8_lossy(&raw[skip..]).to_string();
    }
    for i in 1..=32 {
        if let Some(rv) = cpu.get(&format!("{},-{}]", base, i)) {
            let name = if rv.kind == r#const::KIND_RWIR && rv.body_len >= 4 {
                let n = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
                String::from_utf8_lossy(&n[4..]).to_string()
            } else {
                let n = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
                String::from_utf8_lossy(n).to_string()
            };
            r.reads.push(Param { name, val_kind: rv.kind.clone(), body_ptr: rv.body_ptr, body_len: rv.body_len });
        }
        if let Some(rv) = cpu.get(&format!("{},{}]", base, i)) {
            let name = if rv.kind == r#const::KIND_RWIR && rv.body_len >= 4 {
                let n = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
                String::from_utf8_lossy(&n[4..]).to_string()
            } else {
                let n = unsafe { std::slice::from_raw_parts(rv.body_ptr, rv.body_len as usize) };
                String::from_utf8_lossy(n).to_string()
            };
            r.writes.push(Param { name, val_kind: rv.kind.clone(), body_ptr: rv.body_ptr, body_len: rv.body_len });
        }
    }
    Some(r)
}
