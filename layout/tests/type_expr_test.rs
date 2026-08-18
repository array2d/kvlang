use kvlang_layout::parser::{self};

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
    assert!(parser::has_errors(&diags), "expected errors for malformed type");
}
