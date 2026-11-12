
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
            | "stringkeymap"
            | "rwir"
            | "rwfunc"
            | "scope"
            | "struct"
            | "time"
            | "time/duration"
    )
}

fn valid_structref(s: &str) -> bool {
    let rest = match s.strip_prefix('/') {
        Some(r) => r,
        None => return false,
    };
    
    !rest.is_empty()
        && rest.split('/').all(|seg| {
            !seg.is_empty()
                && !super::scanner::has_token_delim(seg)
                && seg.bytes().all(|b| b != b'"' && b != b'\'')
        })
}

fn valid_base(s: &str) -> bool {
    if s.is_empty() {
        return false;
    }
    known_kind(s)
}

fn elem_is_variable(s: &str) -> bool {
    s == "?" || s == "*" || s == "+"
}

fn valid_elem(s: &str) -> bool {
    if s.is_empty() {
        return false;
    }
    matches!(s, "." | "?" | "*" | "+") || s.bytes().all(|b| b.is_ascii_digit())
}

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

fn valid_key(s: &str) -> bool {
    
    if valid_scalar(s) {
        return true;
    }
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

fn valid_atom(s: &str) -> bool {
    
    if let Some(rest) = s.strip_prefix('*').or_else(|| s.strip_prefix('@')) {
        
        return rest.is_empty() || valid_atom(rest);
    }
    if s.starts_with('/') {
        return valid_structref(s);
    }
    if let Some(i) = s.find('·') {
        return valid_key(&s[..i]) && valid_langtype(&s[i + '·'.len_utf8()..]);
    }
    valid_shape(s)
}

pub fn valid_langtype(expr: &str) -> bool {
    
    if let Some(inner) = expr.strip_prefix('(').and_then(|r| r.strip_suffix(')')) {
        return valid_langtype(inner);
    }
    
    if crate::kvkind::is_map_langtype(expr) {
        let Some(closing) = expr[1..].find('\'') else {
            return false; 
        };
        let value = &expr[closing + 2..];
        return !value.is_empty() && !value.contains('\'');
    }
    expr.is_empty() || expr.split('|').all(valid_atom)
}

pub fn has_union(expr: &str) -> bool {
    expr.contains('|')
}

pub fn is_plain_kind(ty: &str) -> bool {
    !ty.is_empty()
        && ty.split('|').all(|a| {
            let a = a.trim_start_matches(['*', '@']);
            known_kind(a)
        })
}

pub fn expand_struct_refs(s: &str) -> String {
    
    if let Some(inner) = s.strip_prefix('*').and_then(|r| r.strip_prefix('(')) {
        if let Some(inner) = inner.strip_suffix(')') {
            return format!("*({})", expand_struct_refs(inner));
        }
    }
    s.split('|').map(expand_atom).collect::<Vec<_>>().join("|")
}

fn expand_atom(s: &str) -> String {
    
    if let Some(base) = s.strip_suffix("...") {
        if base.is_empty() || known_kind(base) {
            return s.to_string();
        }
        return format!("{}...", expand_atom(base));
    }
    if let Some(rest) = s.strip_prefix('*') {
        return format!("*{}", expand_atom(rest));
    }
    if let Some(rest) = s.strip_prefix('@') {
        return format!("@{}", expand_atom(rest));
    }
    if s.starts_with('/') {
        return s.to_string();
    }
    
    if crate::kvkind::is_map_langtype(s) {
        if let Some(closing) = s[1..].find('\'') {
            let key = &s[1..closing + 1];
            let value = &s[closing + 2..];
            return format!("'{key}'{}", expand_struct_refs(value));
        }
        return s.to_string(); 
    }
    
    if let Some(i) = s.find('·') {
        let key = &s[..i];
        if valid_key(key) {
            return format!("{key}·{}", expand_struct_refs(&s[i + '·'.len_utf8()..]));
        }
    }
    if s.starts_with('[') {
        return s.to_string();
    }
    
    if let Some(inner) = s.strip_prefix('(').and_then(|r| r.strip_suffix(')')) {
        return format!("({})", expand_struct_refs(inner));
    }
    if s.is_empty() || known_kind(s) {
        return s.to_string();
    }
    format!("/lib/{s}")
}

fn base_match(s: &str, kind: &str) -> bool {
    s == kind
}

fn fixed_elem_match(e: &str, d: i32) -> bool {
    e == "." || e.parse::<i32>().ok() == Some(d)
}

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
            let f = (m - 1) as i32; 
            let rem = ndim - f; 
            let ok_rem = match elems[vpos] {
                "?" => rem == 0 || rem == 1,
                "*" => rem >= 0,
                _ => rem >= 1, 
            };
            if !ok_rem {
                return false;
            }
            
            for (i, e) in elems.iter().take(vpos).enumerate() {
                if !fixed_elem_match(e, dims[i]) {
                    return false;
                }
            }
            
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

fn match_atom(s: &str, kind: &str, ndim: i32, dims: &[i32]) -> bool {
    
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

pub fn match_langtype(expr: &str, kind: &str, ndim: i32, dims: &[i32]) -> bool {
    
    expr.is_empty()
        || expr
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
            "char/utf8",
            "char/utf32",
            "char/ascii",
            "stringkeymap",
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
            "int64|stringkeymap",
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
            
            "*int64",
            "@int64",
            "@[256,256]uint8",
            "*[int32,int32]·[]char/utf8",
            "",             
            "[]char/utf8·", 
            "*",            
        ] {
            assert!(valid_langtype(e), "{e} should be valid");
        }
        for e in [
            "·int64",
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
    }

    #[test]
    fn quantifiers() {
        
        assert!(match_langtype("[.]int64", "int64", 1, &[5]));
        assert!(!match_langtype("[.]int64", "int64", 2, &[2, 3]));
        
        assert!(match_langtype("[*]int64", "int64", 0, &[]));
        assert!(match_langtype("[*]int64", "int64", 3, &[2, 3, 4]));
        
        assert!(!match_langtype("[+]int64", "int64", 0, &[]));
        assert!(match_langtype("[+]int64", "int64", 2, &[2, 3]));
        
        assert!(match_langtype("[?,3]int64", "int64", 1, &[3]));
        assert!(match_langtype("[?,3]int64", "int64", 2, &[7, 3]));
        assert!(!match_langtype("[?,3]int64", "int64", 3, &[7, 8, 3]));
        
        assert!(match_langtype("[512,*]int64", "int64", 1, &[512]));
        assert!(match_langtype("[512,*]int64", "int64", 3, &[512, 8, 8]));
        assert!(!match_langtype("[512,*]int64", "int64", 2, &[7, 8]));
    }

    #[test]
    fn invalid() {
        for e in [
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
            "any",
            "int64|any",
            "[]any",
            "*any",
        ] {
            assert!(!valid_langtype(e), "{e} should be invalid");
        }
    }

    #[test]
    fn matching() {
        let cases = [
            ("int64", "int64", 0, &[][..], true),
            ("int64", "float64", 0, &[], false),
            ("", "stringkeymap", 0, &[], true),
            ("", "int4", 0, &[], true),
            ("", "int64", 0, &[], true),
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
            ("int64|stringkeymap", "int64", 0, &[], true),
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
