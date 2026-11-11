//! builtin 辅助（对齐 rwir/builtin 中 lower/layout 依赖的部分：
//! NumOp/IsNumKind/WiderNumKind/OpKind/TryParseNumber）。

use super::ffi;

// ── 数值 kind 工具 ───────────────────────────────────────────────────

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

// ── 字面量解析 ───────────────────────────────────────────────────────

/// 数值字面量 wire + langtype（attr key 精确匹配 wire 的 kind）。
/// Returns `Err` on kvspace codec failure and `Ok(None)` for non-numeric tokens.
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

// ── 输出算子的保留 opid ──────────────────────────────────────────────

/// 输出算子的**保留区 opid**（0xFF01–0xFF03）。
///
/// 这张表是「用户可见名 → 指令编号」的**唯一映射点**。layout 在指令槽里写
/// **十进制字符串**（`println` → `"65281"`，即 0xFF01），runtimec 的 encoder 只按「槽名
/// 全为数字即 opid」处理，一个算子名也不认识。这样：
///   - wire 形状不变（指令槽仍是字符串），不需要动 binir 格式；
///   - runtimec 里出现不了 `println` / `print` / `cerr` 这三个词，也没有名字表；
///   - 「谁是输出算子」的知识只在一个仓库里存在一次。
///
/// ## 编号从哪来
///
/// `runtimec/binrwir/instruction.h` 的保留区占用记账：
///
/// ```text
/// 保留区占用记账（0xFF00–0xFFFF）：
///   0xFF00–0xFF03  临时 println 三条（print / println / cerr）
///   0xFFFE         FOREIGN
///   0xFFFF         NONE（非法指令头）
/// ```
///
/// **那份记账是编号的权威**，本表只是它的消费者。记账变了，这里必须跟着变——
/// 两边漂移的表现是「指令头写了保留区编号，执行器按别的算子解释」，静默错算。
///
/// ## 临时性
///
/// 这三个算子在 runtimec 侧是**临时实现**：整段由 `RUNTIMEC_TEMP_OUTPUT` 编译期
/// 开关控制、默认 OFF，产物里 grep 不到任何 `println` 痕迹。正式实现归 byteseek
/// 扩展 runtime 所有（issue #622 决策 D）——那三个 opcode 刻意不进 runtimec 的
/// myrwircaps 能力表。
///
/// 所以本表**不是**「runtimec 正式的算子编号表」，而是「临时实现期两侧共用的
/// 编号约定」。等临时实现退役，本表也应随之消失或改指向扩展侧编号。
///
/// ## 匹配规则
///
/// **只对裸名精确匹配**。带包前缀的 `x·println` 是另一条 rwir，不是内建输出——
/// 若按后缀匹配会把用户自己声明的 `pkg·print` 也编成 opid，那等于悄悄改写了
/// 用户代码的语义。
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
        // 十进制值就是 layout 写进指令槽的字面量，两者必须对得上。
        /* 0xFF01 / 0xFF02 / 0xFF03 的十进制：65281 / 65282 / 65283。
         * 早先这里写过 16641/16642/16643，那是 0x4101–0x4103——**不在保留区
         * 0xFF00–0xFFFF 之内**，会被执行器当成普通算子编号，与真实能力行相撞。
         * 十进制转换必须实算，不能凭印象。 */
        assert_eq!(output_opid("println"), Some(65281));
        assert_eq!(output_opid("print"), Some(65282));
        assert_eq!(output_opid("cerr"), Some(65283));
        assert_eq!(output_opid("println").unwrap().to_string(), "65281");
        assert_eq!(output_opid("print").unwrap().to_string(), "65282");
        assert_eq!(output_opid("cerr").unwrap().to_string(), "65283");
    }

    #[test]
    fn output_opid_is_exact_match_only() {
        // 带包前缀的不是内建输出：它是用户声明的另一条 rwir。
        assert_eq!(output_opid("x·println"), None);
        assert_eq!(output_opid("mypkg·print"), None);
        // 相似但不同的名字不能命中。
        assert_eq!(output_opid("printlns"), None);
        assert_eq!(output_opid("Print"), None);
        assert_eq!(output_opid(""), None);
        // 普通算子不受影响。
        assert_eq!(output_opid("add"), None);
        assert_eq!(output_opid("kvspace·listlen"), None);
    }
}
