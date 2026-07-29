//! Op dispatch router — identical to op/dispatch/dispatch.go and op/dispatch/dispatch.h.
pub enum Route { Builtin, VTypeOp, ControlFlow, Call, Unknown }
pub fn route_op(opcode: &str) -> Route {
    if opcode.starts_with("/lib/") { Route::Call }
    else if ["goto","br","return","label"].contains(&opcode) { Route::ControlFlow }
    else { Route::Builtin }
}
