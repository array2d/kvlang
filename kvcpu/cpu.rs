//! KV Virtual CPU — identical to kvcpu/cpu.go and kvcpu/cpu.h.
//! Primary implementation language: Rust.

/// CPU is the KV virtual CPU interface.
pub trait Cpu {
    fn execute(&mut self, pc: &str) -> Result<(), String>;
    fn step(&mut self, pc: &str) -> Result<(), String>;
    fn debugger_active(&self) -> bool;
}
