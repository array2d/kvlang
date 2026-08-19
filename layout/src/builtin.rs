//! builtin 辅助（对齐 rwir/builtin 中 lower/layout 依赖的部分：
//! NumOp/IsNumKind/WiderNumKind/OpKind/TryParseNumber/IsNativeRwir/IsGlobalRwir）。

use super::ffi;

// ── 数值 kind 工具 ───────────────────────────────────────────────────

pub fn is_int_kind(k: &str) -> bool {
    matches!(k, "int8" | "int16" | "int32" | "int64" | "uint8" | "uint16" | "uint32" | "uint64")
}

pub fn is_float_kind(k: &str) -> bool {
    k == "float32" || k == "float64"
}

pub fn is_unsigned_kind(k: &str) -> bool {
    matches!(k, "uint8" | "uint16" | "uint32" | "uint64")
}

pub fn is_num_kind(k: &str) -> bool {
    is_int_kind(k) || is_float_kind(k)
}

pub fn int_kind_width(k: &str) -> i32 {
    match k {
        "int8" | "uint8" => 8,
        "int16" | "uint16" => 16,
        "int32" | "uint32" => 32,
        "int64" | "uint64" => 64,
        _ => 0,
    }
}

pub fn wider_int_kind(ak: &str, bk: &str) -> String {
    let (aw, bw) = (int_kind_width(ak), int_kind_width(bk));
    let (au, bu) = (is_unsigned_kind(ak), is_unsigned_kind(bk));
    if au == bu {
        return if aw >= bw { ak.to_string() } else { bk.to_string() };
    }
    let w = aw.max(bw);
    match w {
        8 => "int16".to_string(),
        16 => "int32".to_string(),
        _ => "int64".to_string(),
    }
}

pub fn wider_float_kind(ak: &str, bk: &str) -> String {
    if ak == "float64" || bk == "float64" {
        "float64".to_string()
    } else if ak == "float32" || bk == "float32" {
        "float32".to_string()
    } else {
        "float64".to_string()
    }
}

pub fn wider_num_kind(ak: &str, bk: &str) -> String {
    if is_int_kind(ak) && is_int_kind(bk) {
        return wider_int_kind(ak, bk);
    }
    if is_float_kind(ak) && is_float_kind(bk) {
        return wider_float_kind(ak, bk);
    }
    if is_float_kind(ak) {
        ak.to_string()
    } else {
        bk.to_string()
    }
}

/// op 在给定输入 kind 下的注册 kind（"" 表示该 kind 无此 op）。
pub fn op_kind(op: &str, k: &str) -> String {
    if !is_num_kind(k) {
        return String::new();
    }
    match op {
        "pow" | "sqrt" | "exp" | "log" => {
            if is_float_kind(k) {
                k.to_string()
            } else {
                "float64".to_string()
            }
        }
        "mod" => {
            if is_int_kind(k) {
                k.to_string()
            } else {
                String::new()
            }
        }
        "bitand" | "bitor" | "bitxor" | "shl" | "shr" => {
            if is_int_kind(k) {
                if is_unsigned_kind(k) {
                    "uint64".to_string()
                } else {
                    "int64".to_string()
                }
            } else {
                String::new()
            }
        }
        "neg" | "abs" => {
            if is_unsigned_kind(k) {
                String::new()
            } else {
                k.to_string()
            }
        }
        _ => k.to_string(),
    }
}

/// 判断 opcode 是否为多态数值 op。
pub fn num_op(opcode: &str) -> bool {
    matches!(
        opcode,
        "add" | "sub" | "mul" | "div" | "neg" | "mod"
            | "bitand" | "bitor" | "bitxor" | "shl" | "shr"
            | "eq" | "neq" | "lt" | "le" | "gt" | "ge"
            | "pow" | "sqrt" | "exp" | "log"
            | "abs" | "sign" | "max" | "min"
    )
}

// ── 字面量解析 ───────────────────────────────────────────────────────

/// 将数字字面量解析为 int64/uint64/float64 的 TLV 字节。
pub fn try_parse_number(s: &str) -> Option<Vec<u8>> {
    if s.is_empty() {
        return None;
    }
    let c0 = s.as_bytes()[0];
    let is_digit = c0.is_ascii_digit();
    let is_neg = c0 == b'-' && s.len() >= 2 && s.as_bytes()[1].is_ascii_digit();
    if !is_digit && !is_neg {
        return None;
    }
    if let Ok(i) = s.parse::<i64>() {
        return Some(ffi::new_int64(i));
    }
    if c0 != b'-' && !s.contains('.') && !s.contains('e') && !s.contains('E') {
        if let Ok(u) = s.parse::<u64>() {
            return Some(ffi::tlv_encode("uint64", &u.to_le_bytes(), 1));
        }
    }
    if let Ok(f) = s.parse::<f64>() {
        return Some(ffi::new_float64(f));
    }
    None
}

// ── native / global rwir 判定（layout 命名空间用） ───────────────────

/// VM 原生 rwir 中不带 `.` 的 opcode（带 `.` 的由 MemberSep 检查兜底）。
fn native_rwir_set() -> &'static [&'static str] {
    &[
        // 单字 builtin
        "array", "at", "set", "has", "dict", "debugger",
        // cast kind
        "bool", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64",
        "float32", "float64", "char/utf8", "char/ascii", "char/utf32",
        // 数值 op word + glyph
        "add", "+", "sub", "-", "mul", "×", "div", "÷", "mod", "%",
        "eq", "==", "neq", "!=", "≠", "lt", "<", "gt", ">", "le", "<=", "≤", "ge", ">=", "≥",
        "and", "&&", "or", "||", "not", "!",
        "bitand", "&", "bitor", "|", "bitxor", "^", "shl", "<<", "shr", ">>",
        "sqrt", "√", "neg", "abs", "sign", "max", "min", "pow", "exp", "log",
        // kv.* 树操作
        "kv.get", "kv.set", "kv.del", "kv.deltree", "kv.list", "kv.mkindex",
        "kv.extindex", "kv.rmindexext", "kv.watch", "kv.has", "kv.at",
        // xv.* 形状内省与多维元素访问
        "xv.numel", "xv.dim", "xv.shape", "xv.at", "xv.set",
    ]
}

pub fn is_native_rwir(opcode: &str) -> bool {
    native_rwir_set().contains(&opcode)
}

fn global_rwir_set() -> &'static [&'static str] {
    &["print", "println", "cerr", "input", "json.to", "json.from"]
}

pub fn is_global_rwir(opcode: &str) -> bool {
    global_rwir_set().contains(&opcode)
}
