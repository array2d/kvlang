
use super::ffi;

pub fn is_int_kind(k: &str) -> bool {
    matches!(
        k,
        "int8" | "int16" | "int32" | "int64" | "uint8" | "uint16" | "uint32" | "uint64"
    )
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
        return if aw >= bw {
            ak.to_string()
        } else {
            bk.to_string()
        };
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

pub fn num_op(opcode: &str) -> bool {
    matches!(
        opcode,
        "add"
            | "sub"
            | "mul"
            | "div"
            | "neg"
            | "mod"
            | "bitand"
            | "bitor"
            | "bitxor"
            | "shl"
            | "shr"
            | "eq"
            | "neq"
            | "lt"
            | "le"
            | "gt"
            | "ge"
            | "pow"
            | "sqrt"
            | "exp"
            | "log"
            | "abs"
            | "sign"
            | "max"
            | "min"
    )
}

pub fn try_parse_number(s: &str) -> Result<Option<(Vec<u8>, &'static str)>, String> {
    if s.is_empty() {
        return Ok(None);
    }
    let c0 = s.as_bytes()[0];
    let is_digit = c0.is_ascii_digit();
    let is_neg = c0 == b'-' && s.len() >= 2 && s.as_bytes()[1].is_ascii_digit();
    if !is_digit && !is_neg {
        return Ok(None);
    }
    if let Ok(i) = s.parse::<i64>() {
        return Ok(Some((ffi::new_int64(i)?, "int64")));
    }
    if c0 != b'-' && !s.contains('.') && !s.contains('e') && !s.contains('E') {
        if let Ok(u) = s.parse::<u64>() {
            return Ok(Some((
                ffi::xvalue_to_wire("uint64", &u.to_le_bytes(), 1)?,
                "uint64",
            )));
        }
    }
    if let Ok(f) = s.parse::<f64>() {
        return Ok(Some((ffi::new_float64(f)?, "float64")));
    }
    Ok(None)
}

pub fn output_opid(name: &str) -> Option<u16> {
    match name {
        "println" => Some(0xFF01),
        "print" => Some(0xFF02),
        "cerr" => Some(0xFF03),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn output_opid_maps_three_bare_names() {
        
        assert_eq!(output_opid("println"), Some(65281));
        assert_eq!(output_opid("print"), Some(65282));
        assert_eq!(output_opid("cerr"), Some(65283));
        assert_eq!(output_opid("println").unwrap().to_string(), "65281");
        assert_eq!(output_opid("print").unwrap().to_string(), "65282");
        assert_eq!(output_opid("cerr").unwrap().to_string(), "65283");
    }

    #[test]
    fn output_opid_is_exact_match_only() {
        
        assert_eq!(output_opid("x·println"), None);
        assert_eq!(output_opid("mypkg·print"), None);
        
        assert_eq!(output_opid("printlns"), None);
        assert_eq!(output_opid("Print"), None);
        assert_eq!(output_opid(""), None);
        
        assert_eq!(output_opid("add"), None);
        assert_eq!(output_opid("pathspace·listlen"), None);
    }
}
