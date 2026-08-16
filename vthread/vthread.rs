//! Virtual thread — matching vthread/vthread.go.
use crate::keytree::r#const as kt;
use crate::kvcpu::cpu::KVCpu;

pub fn set(cpu: &KVCpu, vtid: &str, pc: &str, status: &str) {
    cpu.set(&kt::vthread_pc(vtid), "string", pc.as_bytes(), 1, false);
    cpu.set(&kt::vthread_status(vtid), "string", status.as_bytes(), 1, false);
}
pub fn set_done(cpu: &KVCpu, vtid: &str) {
    cpu.set(&kt::vthread_status(vtid), "string", b"done", 1, false);
}
