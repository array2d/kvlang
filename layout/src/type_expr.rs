//! 签名类型表达式（runtime篇-07，修订：无家族简写）：语法校验 + 值匹配。
//!
//! type   = atom ("|" atom)*
//! atom   = [dims] ( any | kind )
//! dims   = "[]" | "[" dim ("," dim)* "]"
//! dim    = integer | "?"
//! any    = "any"           # 通配，匹配任意 kind
//! kind   = 精确 kind 串    # 见 [`known_kind`]
//!
//! 铁律：不提供 int/uint/float/num 数值家族（位宽开放，int4/fp8/fp16…），
//! 也不提供 char 编码简写（编码须写明确，如 char/utf8、char/utf32）。多态靠显式 "|" 枚举。

/// 精确 kind 集合（对齐 runtime kind 常量，不含 None）。
fn known_kind(k: &str) -> bool {
    matches!(
        k,
        "bool" | "int8" | "int16" | "int32" | "int64" | "uint8" | "uint16" | "uint32" | "uint64"
            | "float32" | "float64" | "char/utf32" | "char/utf8" | "char/ascii" | "dict" | "index"
            | "extindex" | "rwir" | "rwfunc" | "scope" | "time" | "duration"
    )
}

fn valid_base(s: &str) -> bool {
    if s.is_empty() {
        return false;
    }
    s == "any" || known_kind(s)
}

fn valid_dim(s: &str) -> bool {
    if s.is_empty() {
        return false;
    }
    s == "?" || s.bytes().all(|b| b.is_ascii_digit())
}

fn valid_dims(s: &str) -> bool {
    s.is_empty() || s.split(',').all(valid_dim)
}

fn valid_atom(s: &str) -> bool {
    if s.is_empty() {
        return false;
    }
    if let Some(rest) = s.strip_prefix('[') {
        let end = match rest.find(']') {
            Some(e) => e,
            None => return false,
        };
        if !valid_dims(&rest[..end]) {
            return false;
        }
        let base = &rest[end + 1..];
        return !base.is_empty() && valid_base(base);
    }
    valid_base(s)
}

/// 末参变参标记：`A:any...` 表 0..N 个同型实参。
pub fn is_variadic(expr: &str) -> bool {
    expr.ends_with("...")
}

/// 去掉尾缀 `...`（非变参原样返回）。
pub fn strip_variadic(expr: &str) -> &str {
    expr.strip_suffix("...").unwrap_or(expr)
}

/// 类型表达式语法校验（装载期）。允许末参尾缀 `...` 变参。
pub fn valid_type_expr(expr: &str) -> bool {
    let e = strip_variadic(expr);
    !e.is_empty() && e.split('|').all(valid_atom)
}

fn base_match(s: &str, kind: &str) -> bool {
    match s {
        "any" => true,
        _ => s == kind,
    }
}

fn match_shape(s: &str, ndim: i32, dims: &[i32]) -> bool {
    if s.is_empty() {
        return ndim >= 1;
    }
    let parts: Vec<&str> = s.split(',').collect();
    if parts.len() as i32 != ndim {
        return false;
    }
    for (i, p) in parts.iter().enumerate() {
        if *p == "?" {
            continue;
        }
        if p.parse::<i32>().ok() != Some(dims[i]) {
            return false;
        }
    }
    true
}

/// ndim = -1 表示「已消费 dims，不再判 ndim」（递归哨兵）。
fn match_atom(s: &str, kind: &str, ndim: i32, dims: &[i32]) -> bool {
    if let Some(rest) = s.strip_prefix('[') {
        let end = match rest.find(']') {
            Some(e) => e,
            None => return false,
        };
        if !match_shape(&rest[..end], ndim, dims) {
            return false;
        }
        return match_atom(&rest[end + 1..], kind, -1, &[]);
    }
    if ndim >= 0 && ndim != 0 {
        return false;
    }
    base_match(s, kind)
}

/// 单值（kind/ndim/dims）是否匹配类型表达式：任一 atom 命中即 true。
/// 变参 `...` 按单元素判定（去尾缀后匹配），重复由派发循环处理。
pub fn match_type(expr: &str, kind: &str, ndim: i32, dims: &[i32]) -> bool {
    let e = strip_variadic(expr);
    !e.is_empty() && e.split('|').any(|atom| match_atom(atom, kind, ndim, dims))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn valid() {
        for e in [
            "int64", "uint8", "float32", "bool", "any",
            "char/utf8", "char/utf32", "char/ascii", "dict", "index",
            "[]float32", "[2]float32", "[2,3]float32", "[2,3,4]float64",
            "[?,768]float32", "[?,?]int8",
            "int64|float64", "[2,3]float32|float32", "[]float32|[]float64",
            "bool|char/utf8", "index|dict",
            "any...", "int64|float64...", "[]float32...",
        ] {
            assert!(valid_type_expr(e), "{e} should be valid");
        }
    }

    #[test]
    fn variadic() {
        assert!(is_variadic("any..."));
        assert!(!is_variadic("any"));
        assert_eq!(strip_variadic("int64|float64..."), "int64|float64");
        // 变参 kindexp 按单元素匹配
        assert!(match_type("any...", "int64", 0, &[]));
        assert!(match_type("int64|float64...", "float64", 0, &[]));
        assert!(!match_type("int64...", "bool", 0, &[]));
    }

    #[test]
    fn invalid() {
        for e in [
            "", "int|", "|int", "int||float64", "|", "[]", "[2]", "[?]",
            "[2", "2]", "[2,]float32", "[,2]float32", "[2 3]float32",
            "*int64", "@int64", "int64*", "float64|", "int ", "float32,float64",
            "int", "uint", "float", "num", "char", "int4", "fp8", "fp16", "string", "charbyte",
        ] {
            assert!(!valid_type_expr(e), "{e} should be invalid");
        }
    }

    #[test]
    fn matching() {
        let cases = [
            ("int64", "int64", 0, &[][..], true),
            ("int64", "float64", 0, &[], false),
            ("any", "dict", 0, &[], true),
            ("any", "int4", 0, &[], true),
            ("char/utf8", "char/utf8", 0, &[], true),
            ("char/utf8", "char/utf32", 0, &[], false),
            ("int64|float64", "float64", 0, &[], true),
            ("int64|float64", "bool", 0, &[], false),
            ("[]float32", "float32", 1, &[5], true),
            ("[2,3]float32", "float32", 2, &[2, 3], true),
            ("[2,3]float32", "float32", 2, &[2, 4], false),
            ("[?,768]float32", "float32", 2, &[100, 768], true),
            ("[?,768]float32", "float32", 2, &[100, 512], false),
            ("[2,3]float32|float32", "float32", 0, &[], true),
            ("[2,3]float32|float32", "float64", 0, &[], false),
            ("[]float32|[]float64", "float64", 1, &[10], true),
            ("bool|char/utf8", "char/utf8", 0, &[], true),
            ("index|dict", "index", 0, &[], true),
        ];
        for (expr, kind, ndim, dims, want) in cases {
            let got = match_type(expr, kind, ndim, dims);
            assert_eq!(got, want, "match_type({expr}, {kind}, ndim={ndim}, dims={dims:?})");
        }
    }
}
