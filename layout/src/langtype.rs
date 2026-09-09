//! 签名 langtype（runtime篇-07，修订：无家族简写）：语法校验 + 值匹配。
//!
//! type   = atom ("|" atom)*
//! atom   = shape | mapexpr
//! shape  = [dims] ( any | kind )
//! mapexpr= key "·" type      # stringkeymap；value 递归 → 嵌套 map[string]map[…]
//! key    = "[]" char-enc     # 字符串键（= Go map[string]V）
//!        | "[" scalar ("," scalar)* "]"   # 标量元组键（物理以字符串格式落 key）
//! dims   = "[]" | "[" elem ("," elem)* "]"   # 至多一个变元量词 ?/*/+
//! elem   = integer | "." | "?" | "*" | "+"   # 轴量词（借鉴正则：. .? .* .+）
//! any    = "any"           # 通配，匹配任意 kind
//! kind   = 精确 kind 串    # 见 [`known_kind`]
//!
//! 铁律：不提供 int/uint/float/num 数值家族简写（位宽开放，纳入须逐个显式命名，
//! 如低精度浮点 float16/bfloat16/float8/e4m3/float8/e5m2；int4 等仍未纳入），
//! 也不提供 char 编码简写（编码须写明确，如 char/utf8、char/utf32）。多态靠显式 "|" 枚举。
//! 变参 "..." 是签名层 arity（吸收 0..N 个实参），不是 langtype 的一部分——由主槽 dynamic 字节承载。

/// 精确 kind 集合（对齐 runtime kind 常量，不含 None）。
fn known_kind(k: &str) -> bool {
    matches!(
        k,
        "bool"
            | "int8"
            | "int16"
            | "int32"
            | "int64"
            | "uint8"
            | "uint16"
            | "uint32"
            | "uint64"
            | "float32"
            | "float64"
            | "float16"
            | "bfloat16"
            | "float8/e4m3"
            | "float8/e5m2"
            | "char/utf32"
            | "char/utf8"
            | "char/ascii"
            | "object"
            | "stringkeymap"
            | "index"
            | "extindex"
            | "rwir"
            | "rwfunc"
            | "scope"
            | "struct"
            | "time"
            | "duration"
    )
}

/// structref = "/" path：指向 /lib 下类型定义节点的完整路径（实例 langtype、struct 字段类型）。
/// 仅做语法承认（`/` + 合法路径段），不解析 /lib 是否存在该类型 —— 存在性/字段一致性留给 runtime。
fn valid_structref(s: &str) -> bool {
    let rest = match s.strip_prefix('/') {
        Some(r) => r,
        None => return false,
    };
    !rest.is_empty()
        && rest.split('/').all(|seg| {
            !seg.is_empty() && seg.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
        })
}

fn valid_base(s: &str) -> bool {
    if s.is_empty() {
        return false;
    }
    s == "any" || known_kind(s)
}

fn elem_is_variable(s: &str) -> bool {
    s == "?" || s == "*" || s == "+"
}

/// elem = integer | "." | "?" | "*" | "+"（借鉴正则的轴量词）。
fn valid_elem(s: &str) -> bool {
    if s.is_empty() {
        return false;
    }
    matches!(s, "." | "?" | "*" | "+") || s.bytes().all(|b| b.is_ascii_digit())
}

/// dims = ε（空 [] = 1 维任意，等价 [.]） | elem ("," elem)*；至多一个变元量词 ?/*/+。
fn valid_dims(s: &str) -> bool {
    if s.is_empty() {
        return true;
    }
    let mut nvar = 0;
    for e in s.split(',') {
        if !valid_elem(e) {
            return false;
        }
        if elem_is_variable(e) {
            nvar += 1;
            if nvar > 1 {
                return false;
            }
        }
    }
    true
}

fn valid_shape(s: &str) -> bool {
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

/// 标量 kind（可作元组键元素；不含 char/object/index 等）。
fn valid_scalar(s: &str) -> bool {
    matches!(
        s,
        "bool"
            | "int8"
            | "int16"
            | "int32"
            | "int64"
            | "uint8"
            | "uint16"
            | "uint32"
            | "uint64"
            | "float32"
            | "float64"
    )
}

/// stringkeymap 的 key 恒 `[…]` 起头，两式：
///   `[]char/<enc>`      —— 字符串键（= Go `map[string]V`）
///   `[T1,T2,…]`         —— 标量元组键（元素为标量 kind，物理以字符串格式落 key）
fn valid_key(s: &str) -> bool {
    let rest = match s.strip_prefix('[') {
        Some(r) => r,
        None => return false,
    };
    let end = match rest.find(']') {
        Some(e) => e,
        None => return false,
    };
    let inner = &rest[..end];
    let tail = &rest[end + 1..];
    if inner.is_empty() {
        return matches!(tail, "char/utf8" | "char/utf32" | "char/ascii");
    }
    tail.is_empty() && inner.split(',').all(valid_scalar)
}

/// atom = shape | mapexpr | structref；mapexpr = key "·" type；structref = "/" path。
fn valid_atom(s: &str) -> bool {
    if s.starts_with('/') {
        return valid_structref(s);
    }
    if let Some(i) = s.find('·') {
        return valid_key(&s[..i]) && valid_langtype(&s[i + '·'.len_utf8()..]);
    }
    valid_shape(s)
}

/// 类型表达式语法校验（装载期）。变参 `...` 是签名层 arity、不入 langtype 串，此处永不见。
pub fn valid_langtype(expr: &str) -> bool {
    !expr.is_empty() && expr.split('|').all(valid_atom)
}

fn base_match(s: &str, kind: &str) -> bool {
    match s {
        "any" => true,
        _ => s == kind,
    }
}

/// 定长元（整数或 `.`）匹配单轴：`.` 任意长，整数须精确相等。
fn fixed_elem_match(e: &str, d: i32) -> bool {
    e == "." || e.parse::<i32>().ok() == Some(d)
}

/// 轴量词序列 → (ndim,dims)。空串等价 `[.]`（恰一维）。至多一个变元量词，切分唯一无回溯。
fn match_shape(s: &str, ndim: i32, dims: &[i32]) -> bool {
    if s.is_empty() {
        return ndim == 1;
    }
    let elems: Vec<&str> = s.split(',').collect();
    let m = elems.len();
    let vpos = elems.iter().position(|e| elem_is_variable(e));
    match vpos {
        None => {
            if ndim as usize != m {
                return false;
            }
            elems
                .iter()
                .enumerate()
                .all(|(i, e)| fixed_elem_match(e, dims[i]))
        }
        Some(vpos) => {
            let f = (m - 1) as i32; // 定长元个数
            let rem = ndim - f; // 变元量词吸收的轴数
            let ok_rem = match elems[vpos] {
                "?" => rem == 0 || rem == 1,
                "*" => rem >= 0,
                _ => rem >= 1, // "+"
            };
            if !ok_rem {
                return false;
            }
            // 前缀定长元 → dims[0..vpos]
            for (i, e) in elems.iter().take(vpos).enumerate() {
                if !fixed_elem_match(e, dims[i]) {
                    return false;
                }
            }
            // 后缀定长元 → dims 末 suf 轴
            let suf = m - 1 - vpos;
            for k in 0..suf {
                if !fixed_elem_match(elems[vpos + 1 + k], dims[ndim as usize - suf + k]) {
                    return false;
                }
            }
            true
        }
    }
}

/// ndim = -1 表示「已消费 dims，不再判 ndim」（递归哨兵）。
fn match_atom(s: &str, kind: &str, ndim: i32, dims: &[i32]) -> bool {
    // mapexpr（含 `·`）：容器值的 kind 恒为 stringkeymap，key/value 型不在此判。
    if s.contains('·') {
        return kind == "stringkeymap";
    }
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
/// 变参 arity 不在此判（靠主槽 dynamic 字节 + 派发循环）。
pub fn match_langtype(expr: &str, kind: &str, ndim: i32, dims: &[i32]) -> bool {
    !expr.is_empty()
        && expr
            .split('|')
            .any(|atom| match_atom(atom, kind, ndim, dims))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn valid() {
        for e in [
            "int64",
            "uint8",
            "float32",
            "bool",
            "any",
            "char/utf8",
            "char/utf32",
            "char/ascii",
            "object",
            "stringkeymap",
            "index",
            "[]float32",
            "[2]float32",
            "[2,3]float32",
            "[2,3,4]float64",
            "[.,768]float32",
            "[.,.]int8",
            "[.]float32",
            "[*]float32",
            "[?,3]float32",
            "[+,3]float32",
            "[2,*]float32",
            "[512,+]float32",
            "int64|float64",
            "[2,3]float32|float32",
            "[]float32|[]float64",
            "bool|char/utf8",
            "index|object",
            "[]char/utf8·int64",
            "[]char/utf32·[]char/utf8",
            "[]char/utf8·[]char/utf8·int64",
            "[int32,int32]·[]char/utf8",
            "[float32,float32]·[]char/utf8",
            "[float32,int8,uint32]·int64",
            "struct",
            "/lib/Point",
            "/lib/geom/Point",
            "/lib/Node",
            "/lib/Point|/lib/Node",
        ] {
            assert!(valid_langtype(e), "{e} should be valid");
        }
        for e in [
            "·int64",
            "[]char/utf8·",
            "char/utf8·int64",
            "int·int64",
            "[]char/utf8·nope",
            "[]int32·int64",
            "[.]·int64",
            "[int32,]·int64",
            "[foo,int32]·int64",
            "any...",
            "int64|float64...",
            "[]float32...",
            "[*,+]int64",
            "[?,*]int64",
            "/",
            "/lib/",
            "/lib//Point",
            "lib/Point",
            "/lib/Po int",
        ] {
            assert!(!valid_langtype(e), "{e} should be invalid");
        }
        assert!(match_langtype("/lib/Point", "/lib/Point", 0, &[]));
        assert!(!match_langtype("/lib/Point", "/lib/Node", 0, &[]));
        assert!(match_langtype("[]char/utf8·int64", "stringkeymap", 1, &[3]));
        assert!(!match_langtype("[]char/utf8·int64", "object", 0, &[]));
    }

    #[test]
    fn quantifiers() {
        // "." 恰 1 轴任意长
        assert!(match_langtype("[.]int64", "int64", 1, &[5]));
        assert!(!match_langtype("[.]int64", "int64", 2, &[2, 3]));
        // "*" 0+ 轴
        assert!(match_langtype("[*]int64", "int64", 0, &[]));
        assert!(match_langtype("[*]int64", "int64", 3, &[2, 3, 4]));
        // "+" 1+ 轴
        assert!(!match_langtype("[+]int64", "int64", 0, &[]));
        assert!(match_langtype("[+]int64", "int64", 2, &[2, 3]));
        // "?" 0 或 1 轴
        assert!(match_langtype("[?,3]int64", "int64", 1, &[3]));
        assert!(match_langtype("[?,3]int64", "int64", 2, &[7, 3]));
        assert!(!match_langtype("[?,3]int64", "int64", 3, &[7, 8, 3]));
        // 前缀定长 + 尾量词
        assert!(match_langtype("[512,*]int64", "int64", 1, &[512]));
        assert!(match_langtype("[512,*]int64", "int64", 3, &[512, 8, 8]));
        assert!(!match_langtype("[512,*]int64", "int64", 2, &[7, 8]));
    }

    #[test]
    fn invalid() {
        for e in [
            "",
            "int|",
            "|int",
            "int||float64",
            "|",
            "[]",
            "[2]",
            "[?]",
            "[2",
            "2]",
            "[2,]float32",
            "[,2]float32",
            "[2 3]float32",
            "*int64",
            "@int64",
            "int64*",
            "float64|",
            "int ",
            "float32,float64",
            "int",
            "uint",
            "float",
            "num",
            "char",
            "int4",
            "fp8",
            "fp16",
            "string",
            "charbyte",
        ] {
            assert!(!valid_langtype(e), "{e} should be invalid");
        }
    }

    #[test]
    fn matching() {
        let cases = [
            ("int64", "int64", 0, &[][..], true),
            ("int64", "float64", 0, &[], false),
            ("any", "object", 0, &[], true),
            ("any", "int4", 0, &[], true),
            ("char/utf8", "char/utf8", 0, &[], true),
            ("char/utf8", "char/utf32", 0, &[], false),
            ("int64|float64", "float64", 0, &[], true),
            ("int64|float64", "bool", 0, &[], false),
            ("[]float32", "float32", 1, &[5], true),
            ("[]float32", "float32", 2, &[2, 3], false),
            ("[]float32", "float32", 0, &[], false),
            ("[2,3]float32", "float32", 2, &[2, 3], true),
            ("[2,3]float32", "float32", 2, &[2, 4], false),
            ("[.,768]float32", "float32", 2, &[100, 768], true),
            ("[.,768]float32", "float32", 2, &[100, 512], false),
            ("[2,3]float32|float32", "float32", 0, &[], true),
            ("[2,3]float32|float32", "float64", 0, &[], false),
            ("[]float32|[]float64", "float64", 1, &[10], true),
            ("bool|char/utf8", "char/utf8", 0, &[], true),
            ("index|object", "index", 0, &[], true),
        ];
        for (expr, kind, ndim, dims, want) in cases {
            let got = match_langtype(expr, kind, ndim, dims);
            assert_eq!(
                got, want,
                "match_langtype({expr}, {kind}, ndim={ndim}, dims={dims:?})"
            );
        }
    }
}
