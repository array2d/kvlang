//! Builtin dispatch table — identical to op/builtin/builtin.go and op/builtin/builtin.h.
pub fn register(_opcode: &str) { todo!("register") }
pub fn lookup(_opcode: &str) -> bool { false }
pub fn dispatch(_opcode: &str) -> String { todo!("dispatch") }
