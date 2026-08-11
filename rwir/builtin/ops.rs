//! Builtin dispatch table — matching rwir/builtin/ops.go.
use std::collections::HashMap;
use crate::kvcpu::cpu::KVCpu;
use super::super::rwir::Param;

pub type BuiltinFn = fn(&KVCpu, &[Param], &[Param], &mut HashMap<String, (String, Vec<u8>)>);

/// Native dispatches an opcode. Returns false if opcode is not a builtin.
pub fn native(cpu: &KVCpu, opcode: &str, reads: &[Param], writes: &[Param],
               vars: &mut HashMap<String, (String, Vec<u8>)>) -> bool {
    match opcode {
        "add" | "+" => super::arith::exec_add(reads, writes, vars),
        "sub" | "-" => super::arith::exec_sub(reads, writes, vars),
        "mul" | "*" | "×" => super::arith::exec_mul(reads, writes, vars),
        "div" | "/" | "÷" => super::arith::exec_div(reads, writes, vars),
        "mod" | "%" => super::arith::exec_mod(reads, writes, vars),
        "neg" => super::arith::exec_neg(reads, writes, vars),
        "eq" | "==" => super::cmp::exec_eq(reads, writes, vars),
        "neq" | "!=" | "≠" => super::cmp::exec_neq(reads, writes, vars),
        "lt" | "<" => super::cmp::exec_lt(reads, writes, vars),
        "gt" | ">" => super::cmp::exec_gt(reads, writes, vars),
        "le" | "<=" | "≤" => super::cmp::exec_le(reads, writes, vars),
        "ge" | ">=" | "≥" => super::cmp::exec_ge(reads, writes, vars),
        "not" | "!" => super::logic::exec_not(reads, writes, vars),
        "and" | "&&" => super::logic::exec_and(reads, writes, vars),
        "or" | "||" => super::logic::exec_or(reads, writes, vars),
        "print" | "println" => { super::io::println_op(reads); }
        "pow" => { super::math::exec_pow(reads, writes, vars); }
        "sqrt" | "√" => { super::math::exec_sqrt(reads, writes, vars); }
        "abs" => { super::math::exec_abs(reads, writes, vars); }
        "time.now" => { super::time::exec_time_now(reads, writes, vars); }
        "time.sub" => { super::time::exec_time_sub(reads, writes, vars); }
        "time.add" => { super::time::exec_time_add(reads, writes, vars); }
        "time.before" => { super::time::exec_time_before(reads, writes, vars); }
        "time.after" => { super::time::exec_time_after(reads, writes, vars); }
        "time.duration.nanos" => { super::time::exec_duration_nanos(reads, writes, vars); }
        "time.duration.millis" => { super::time::exec_duration_millis(reads, writes, vars); }
        "time.duration.seconds" => { super::time::exec_duration_seconds(reads, writes, vars); }
        "time.duration.as_nanos" => { super::time::exec_duration_as_nanos(reads, writes, vars); }
        "time.duration.as_millis" => { super::time::exec_duration_as_millis(reads, writes, vars); }
        "int8" | "int16" | "int32" | "int64" | "uint8" | "uint16" | "uint32" | "uint64"
        | "float32" | "float64" | "bool" | "string" | "char" => super::cast::exec_cast(reads, writes, vars),
        "bitand" | "&" => super::bit::exec_bitand(reads, writes, vars),
        "bitor" | "|" => super::bit::exec_bitor(reads, writes, vars),
        "bitxor" | "^" => super::bit::exec_bitxor(reads, writes, vars),
        "shl" | "<<" => super::bit::exec_shl(reads, writes, vars),
        "shr" | ">>" => super::bit::exec_shr(reads, writes, vars),
        "at" => super::string::exec_at(reads, writes, vars),
        "len" | "string.len" => super::string::exec_len(reads, writes, vars),
        "concat" => super::string::exec_concat(reads, writes, vars),
        "set" | "=" => { super::kvop::exec_set(cpu, reads, writes, vars); }
        "kvhas" => { super::kvop::exec_kvhas(cpu, reads, writes, vars); }
        "exp" => { super::math::exec_exp(reads, writes, vars); }
        "log" => { super::math::exec_log(reads, writes, vars); }
        "time.duration.before" => { super::time::exec_duration_before(reads, writes, vars); }
        "time.duration.after" => { super::time::exec_duration_after(reads, writes, vars); }
        "dict" | "array" => {} // stub: these are complex, pass-through for now
        "call" => { /* handled in controlflow */ return false; }
        "goto" | "br" => { /* handled in controlflow */ return false; }
        "return" => { /* handled in controlflow */ return false; }
        _ => return false,
    };
    true
}
