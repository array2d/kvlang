//! Value type system — identical to vtype/vtype.go and vtype/vtype.h.
pub trait VType {
    fn name(&self) -> &str;
    fn opcode(&self) -> &str;
}
