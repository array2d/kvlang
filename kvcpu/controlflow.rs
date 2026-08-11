//! Control flow handling — call, return, br, goto.
use super::cpu::KVCpu;
use crate::rwir::rwir::Rwir;

/// call func: push new frame on stack, jump to func entry point
pub fn handle_call(cpu: &KVCpu, pc: &str, inst: &Rwir) -> Result<(), String> {
    if inst.reads.is_empty() { return Err("call needs func name".into()); }
    let func_name = &inst.reads[0].name;
    let vtid = pc.split("/[").next().unwrap_or("").trim_start_matches("/vthread/");
    let vt_root = format!("/vthread/{}", vtid);

    // Look up function entry PC
    let func_key = format!("/lib/{}", func_name);
    let callpc_key = format!("{}.callpc", func_key);
    if let Some(rv) = cpu.get(&callpc_key) {
        let entry = unsafe { String::from_utf8_lossy(rv.bytes()).to_string() };
        let new_pc = format!("{}/[0,0]", entry);
        cpu.set(&format!("{}/pc", vt_root), "string", new_pc.as_bytes());
        cpu.set(&format!("{}/status", vt_root), "string", b"running");
        return Ok(());
    }
    Err(format!("call: func {} not found", func_name))
}

/// return: pop frame, jump to parent
pub fn handle_return(cpu: &KVCpu, pc: &str) -> Result<(), String> {
    let vtid = pc.split("/[").next().unwrap_or("").trim_start_matches("/vthread/");
    let vt_root = format!("/vthread/{}", vtid);

    // Find parent frame: strip last /[i,j] segment
    if let Some(lb) = pc.rfind("/[") {
        let parent_root = &pc[..lb];
        let ret_key = format!("{}.returnpc", parent_root);
        if let Some(rv) = cpu.get(&ret_key) {
            let parent_pc = unsafe { String::from_utf8_lossy(rv.bytes()).to_string() };
            if parent_pc.is_empty() {
                cpu.set(&format!("{}/status", vt_root), "string", b"done");
            } else {
                cpu.set(&format!("{}/pc", vt_root), "string", parent_pc.as_bytes());
            }
            return Ok(());
        }
    }
    cpu.set(&format!("{}/status", vt_root), "string", b"done");
    Ok(())
}

/// br(cond, true_label, false_label)
pub fn handle_br(cpu: &KVCpu, pc: &str, inst: &Rwir) -> Result<(), String> {
    if inst.reads.len() < 3 { return Err("br needs 3 args".into()); }
    let cond = unsafe { inst.reads[0].bool() };
    let label_idx = if cond { 1 } else { 2 };
    handle_goto(cpu, pc, &Rwir {
        opcode: "goto".into(),
        reads: vec![inst.reads[label_idx].clone()],
        writes: vec![],
    })
}

/// goto(label): jump to scope
pub fn handle_goto(cpu: &KVCpu, pc: &str, inst: &Rwir) -> Result<(), String> {
    if inst.reads.is_empty() { return Err("goto needs label".into()); }
    let label = unsafe { String::from_utf8_lossy(inst.reads[0].bytes()).to_string() };
    let vtid = pc.split("/[").next().unwrap_or("").trim_start_matches("/vthread/");
    let vt_root = format!("/vthread/{}", vtid);

    // Look for label in current frame scope
    let frame_root = if let Some(lb) = pc.rfind("/[") { &pc[..lb] } else { pc };
    let scope_pc = format!("{}/{}", frame_root, label);
    cpu.set(&format!("{}/pc", vt_root), "string", scope_pc.as_bytes());
    Ok(())
}
