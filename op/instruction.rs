//! Opcode instruction types — identical to op/instruction.go and op/instruction.h.
#[derive(Debug, Clone)]
pub struct Instruction {
    pub opcode: String,
    pub reads: Vec<String>,
    pub write: String,
    pub label: String,
    pub comment: String,
}

impl Instruction {
    pub fn is_call(&self) -> bool { self.opcode.starts_with("/lib/") }
    pub fn is_return(&self) -> bool { self.opcode == "return" }
    pub fn is_goto(&self) -> bool { self.opcode == "goto" }
    pub fn is_terminator(&self) -> bool { self.is_return() || self.is_goto() || self.opcode == "br" }
}

pub const OP_COPY: &str = "copy";
pub const OP_CALL: &str = "call";
pub const OP_RETURN: &str = "return";
pub const OP_GOTO: &str = "goto";
pub const OP_BR: &str = "br";
pub const OP_NOP: &str = "nop";
