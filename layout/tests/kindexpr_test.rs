use kvlanglayout::parser::{self};

#[test]
fn parse_type_expression_signature() {
    let src = "rwfunc f(A:int64|float64, B:[2,3]float32, C:[?,768]float32) -> (D:[]float32) {\n    A -> D\n}\n";
    let (file, diags) = parser::parse_code(src).unwrap();
    let msgs: Vec<String> = diags.iter().map(|d| d.message.clone()).collect();
    assert!(!parser::has_errors(&diags), "unexpected errors: {:?}", msgs);

    let sig = &file.funcs[0].sig;
    assert_eq!(sig.name, "f");
    let tys: Vec<&str> = sig.params.iter().map(|p| p.ty.as_str()).collect();
    assert_eq!(tys, vec!["int64|float64", "[2,3]float32", "[?,768]float32"]);
    let rets: Vec<&str> = sig.returns.iter().map(|p| p.ty.as_str()).collect();
    assert_eq!(rets, vec!["[]float32"]);
}

#[test]
fn reject_malformed_type_expression() {
    let src = "rwfunc f(A:[2,3) -> () {\n}\n";
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

/// 字面量右值的类型由字面量自己给出（`1` 即 int64），写目标标注必须是已知种类名——
/// 裸名 `int`/`intg64` 经 expand_struct_refs 成了 `/lib/int`，不是种类名，须按非法种类拒绝
/// （见 spec 类型系统/文法与合法性）。layout 只判种类名，不查 kvspace 原型是否存在。
#[test]
fn reject_non_kind_annotation_on_literal() {
    for ty in [
        "int", "intg64", "uint", "float", "num", "char", "int4", "string", "[]int", "[2]float",
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

/// 已知种类名、`any`，以及容器/结构字面量的对应标注，都不得误伤。
#[test]
fn accept_known_kinds_on_literal() {
    let cases = [
        "\tx:int64 = 7\n",
        "\tx:float64 = 7\n",
        "\tx:any = 7\n",
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

/// 非字面量右值无从推断类型，放行（不因标注而报错）。
#[test]
fn accept_non_literal_rhs() {
    let src = "rwfunc f() -> () {\n\t7 -> y\n\tx:int = y\n}\n";
    assert_eq!(errors(src), Vec::<String>::new());
}

/// struct 字段默认值是同一构造（标注 + 字面量），同一套判定。
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
    // 合法字段默认值不得误伤：种类名、字符串、指针空值（None 非标量，无从推断故放行）。
    let ok = "struct S {\n\tf:int64=0\n\tg:float64=0.0\n\th:bool=false\n\tn:[]char/utf32=\"a\"\n}\nstruct N {\n\tv:int64=0\n\tnext:*N=None\n}\n";
    assert_eq!(errors(ok), Vec::<String>::new());
}

/// 大括号右值把标注归到「struct 名」一类，裸名在那里**合法**：`x:int = {}` layout 放行，
/// `/lib/int` 是不是 struct 由 runtime 判（`x:int = 1` 才由 layout 拒）。
#[test]
fn accept_bare_name_as_struct_on_brace_literal() {
    let src = "rwfunc f() -> () {\n\tx:int = {}\n\ty:intg64 = {a=1}\n\tp:Point = {}\n}\nstruct Point {\n\tx:int64=0\n}\n";
    assert_eq!(errors(src), Vec::<String>::new());
}

/// 参数名在函数内全局唯一：读参列表内、写参列表内、读写之间均不得同名（见 spec layout语义/函数）。
/// **调用**时不受限——同一变量可同时占读槽与写槽（`inc(x) -> x`），那是调用点的事，不进本检查。
#[test]
fn reject_duplicate_param_names() {
    let cases = [
        (
            "rwfunc f(a:int64, a:int64) -> () {\n}\n",
            "duplicate param \"a\" in read-params",
        ),
        (
            "rwfunc f() -> (a:int64, a:int64) {\n}\n",
            "duplicate param \"a\" in write-params",
        ),
        (
            "rwfunc f(a:int64) -> (a:int64) {\n}\n",
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

/// 合法的参数名组合不得误伤，调用点同名（读写槽同一变量）也不受影响。
#[test]
fn accept_unique_param_names() {
    let sig = "rwfunc f(a:int64, b:int64) -> (c:int64, d:int64) {\n\ta + b -> c\n\t0 -> d\n}\n";
    assert_eq!(errors(sig), Vec::<String>::new());
    let call = "rwfunc inc(a:int64) -> (b:int64) {\n\ta + 1 -> b\n}\nrwfunc f() -> () {\n\t5 -> x\n\tinc(x) -> x\n}\n";
    assert_eq!(errors(call), Vec::<String>::new());
}
