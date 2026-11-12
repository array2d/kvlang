use kvlanglayout::parser::{self};

#[test]
fn parse_type_expression_signature() {
    
    let src = "rwfunc f(*int64 #A, *[2,3]float32 #B, *[?,768]float32 #C) -> (*[]float32 #D) {\n    *A -> *D\n}\n";
    let (file, diags) = parser::parse_code(src).unwrap();
    let msgs: Vec<String> = diags.iter().map(|d| d.message.clone()).collect();
    assert!(!parser::has_errors(&diags), "unexpected errors: {:?}", msgs);

    let sig = &file.funcs[0].sig;
    assert_eq!(sig.name, "f");
    let tys: Vec<&str> = sig.params.iter().map(|p| p.ty.as_str()).collect();
    assert_eq!(tys, vec!["int64", "[2,3]float32", "[?,768]float32"]);
    let rets: Vec<&str> = sig.returns.iter().map(|p| p.ty.as_str()).collect();
    assert_eq!(rets, vec!["[]float32"]);
}

#[test]
fn reject_malformed_type_expression() {
    let src = "rwfunc f(*[2,3 #A) -> () {\n}\n";
    let (_, diags) = parser::parse_code(src).unwrap();
    assert!(
        parser::has_errors(&diags),
        "expected errors for malformed type"
    );
}

fn errors(src: &str) -> Vec<String> {
    let (_, diags) = parser::parse_code(src).unwrap();
    diags
        .iter()
        .filter(|d| !d.warn && !d.info)
        .map(|d| d.message.clone())
        .collect()
}

#[test]
fn reject_non_kind_annotation_on_literal() {
    for ty in [
        "int", "intg64", "uint", "float", "num", "char", "int4", "string", "[]int", "[2]float",
        
        "any",
    ] {
        let rhs = if ty.starts_with('[') { "[1]" } else { "1" };
        let src = format!("rwfunc f() -> () {{\n\tx:{ty} = {rhs}\n}}\n");
        let msgs = errors(&src);
        assert!(
            msgs.iter()
                .any(|m| m.contains(&format!("unknown type {ty:?}"))),
            "{src} should reject {ty:?}, got {msgs:?}"
        );
    }
}

#[test]
fn accept_known_kinds_on_literal() {
    let cases = [
        "\tx:int64 = 7\n",
        "\tx:float64 = 7\n",
        "\ts:char/utf8 = \"hi\"\n",
        "\tx:[]int64 = [1, 2]\n",
        "\tx:[2]float32 = [1.0, 2.0]\n",
        "\tp:Point = {x=1}\n",
        "\tm:[]char/utf8·int64 = {}\n",
    ];
    for body in cases {
        let src = format!("struct Point {{\n\tx:int64=0\n}}\nrwfunc f() -> () {{\n{body}}}\n");
        assert_eq!(errors(&src), Vec::<String>::new(), "src: {src}");
    }
}

#[test]
fn reject_any_annotation_on_literal() {
    let src = "rwfunc f() -> () {\n\tx:any = 7\n}\n";
    let msgs = errors(src);
    assert!(
        msgs.iter().any(|m| m.contains("unknown type \"any\"")),
        "x:any 应报 unknown type，got {msgs:?}"
    );
    assert!(
        msgs.iter()
            .any(|m| m.contains("empty kindexp is only allowed in signature slots")),
        "x:any 不得按空 kindexp 放行，got {msgs:?}"
    );
}

#[test]
fn accept_non_literal_rhs() {
    let src = "rwfunc f() -> () {\n\t7 -> y\n\tx:int = y\n}\n";
    assert_eq!(errors(src), Vec::<String>::new());
}

#[test]
fn reject_non_kind_struct_field_default() {
    for ty in ["int", "intg64", "float", "uint"] {
        let src = format!("struct S {{\n\tf:{ty}=0\n}}\n");
        let msgs = errors(&src);
        assert!(
            msgs.iter()
                .any(|m| m.contains("struct field") && m.contains(&format!("unknown type {ty:?}"))),
            "{src} should reject {ty:?}, got {msgs:?}"
        );
    }
    
    let ok = "struct S {\n\tf:int64=0\n\tg:float64=0.0\n\th:bool=false\n\tn:[]char/utf32=\"a\"\n}\nstruct N {\n\tv:int64=0\n\tnext:*N=None\n}\n";
    assert_eq!(errors(ok), Vec::<String>::new());
}

#[test]
fn accept_bare_name_as_struct_on_brace_literal() {
    let src = "rwfunc f() -> () {\n\tx:int = {}\n\ty:intg64 = {a=1}\n\tp:Point = {}\n}\nstruct Point {\n\tx:int64=0\n}\n";
    assert_eq!(errors(src), Vec::<String>::new());
}

#[test]
fn reject_duplicate_param_names() {
    let cases = [
        (
            "rwfunc f(*int64 #a, *int64 #a) -> () {\n}\n",
            "duplicate param \"a\" in read-params",
        ),
        (
            "rwfunc f() -> (*int64 #a, *int64 #a) {\n}\n",
            "duplicate param \"a\" in write-params",
        ),
        (
            "rwfunc f(*int64 #a) -> (*int64 #a) {\n}\n",
            "appears in both read-params and write-params",
        ),
    ];
    for (src, want) in cases {
        let msgs = errors(src);
        assert!(
            msgs.iter().any(|m| m.contains(want)),
            "{src} should report {want:?}, got {msgs:?}"
        );
    }
}

#[test]
fn accept_unique_param_names() {
    let sig = "rwfunc f(*int64 #a, *int64 #b) -> (*int64 #c, *int64 #d) {\n\t*a + *b -> *c\n\t0 -> *d\n}\n";
    assert_eq!(errors(sig), Vec::<String>::new());
    let call = "rwfunc inc(*int64 #a) -> (*int64 #b) {\n\t*a + 1 -> *b\n}\nrwfunc f() -> () {\n\t5 -> x\n\tinc(x) -> x\n}\n";
    assert_eq!(errors(call), Vec::<String>::new());
}
