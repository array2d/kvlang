
use pathlanglayout::{compile, init_dirs, kvkind, Kv};

struct TempStore(String);

impl Drop for TempStore {
    fn drop(&mut self) {
        pathlanglayout::ffi::remove_test_store(&self.0);
    }
}

fn fresh_kv(tag: &str) -> (TempStore, Kv) {
    let dsn = pathlanglayout::ffi::test_dsn(&format!("uintlit_{tag}"));
    let store = TempStore(dsn.clone());
    pathlanglayout::ffi::remove_test_store(&dsn);

    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    (store, kv)
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

fn ops(kv: &mut Kv, func: &str) -> Vec<String> {
    let prefix = format!("/lib/{func}/");
    let children = kv.list(&prefix, false, false);
    let mut out = Vec::new();
    for c in children {
        let t = c.trim_end_matches('/');
        if !t.starts_with('[') || !t.ends_with(",0]") {
            continue;
        }
        out.push(slot_text(kv, &format!("{prefix}{c}")));
    }
    out
}

fn literal_kinds(kv: &mut Kv, func: &str) -> Vec<String> {
    let prefix = format!("/lib/{func}/");
    let mut out = Vec::new();
    for c in kv.list(&prefix, false, false) {
        let t = c.trim_end_matches('/');
        if !t.starts_with('[') || !t.contains(",-") {
            continue;
        }
        let key = format!("{prefix}{c}");
        let lt = kv.langtype_of(&key);
        if !lt.is_empty() && lt != "rwir" {
            out.push(lt);
        }
    }
    out
}

fn write_kind(kv: &mut Kv, key: &str) -> String {
    let text = slot_text(kv, key);
    text.rsplit('\0').next().unwrap_or("").to_string()
}

#[test]
fn cast_plus_literal_adapts_to_unsigned() {
    for src in [
        "rwfunc f() -> () {\n    uint64(5) + 1 -> r\n}\n",
        "rwfunc f() -> () {\n    1 + uint64(5) -> r\n}\n",
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u + 1 -> r\n}\n",
    ] {
        let (_store, mut kv) = fresh_kv("cast");
        compile(&mut kv, src).unwrap();
        let os = ops(&mut kv, "f");
        assert!(
            os.iter().any(|o| o == "add"),
            "should emit semantic operator add: {os:?} src={src}"
        );
        assert!(
            literal_kinds(&mut kv, "f").contains(&"uint64".to_string()),
            "literal operand should be lowered as uint64: src={src}"
        );
    }
}

#[test]
fn write_slot_kind_matches_specialized_op() {
    let (_store, mut kv) = fresh_kv("kind");
    compile(&mut kv, "rwfunc f() -> () {\n    uint64(5) + 1 -> r\n}\n").unwrap();
    
    let k = write_kind(&mut kv, "/lib/f/[2,1]");
    assert_eq!(k, "uint64", "write slot kind should be uint64, got {k}");
}

#[test]
fn uint8_literal_boundary_fits_and_overflows() {
    let (_store, mut kv) = fresh_kv("bound");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint8 = 200\n    u + 255 -> r\n}\n",
    )
    .unwrap();
    assert!(
        literal_kinds(&mut kv, "f").contains(&"uint8".to_string()),
        "255 should be lowered as uint8"
    );

    let (_store, mut kv) = fresh_kv("overflow");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint8 = 200\n    u + 256 -> r\n}\n",
    )
    .unwrap();
    assert!(
        !literal_kinds(&mut kv, "f").contains(&"uint8".to_string()),
        "256 overflows uint8 and must not be adapted"
    );
}

#[test]
fn float_still_wins_over_literal_adaptation() {
    let (_store, mut kv) = fresh_kv("float");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u + 1.0 -> r\n}\n",
    )
    .unwrap();
    assert!(
        literal_kinds(&mut kv, "f").contains(&"float64".to_string()),
        "presence of float should promote to float64"
    );
}

#[test]
fn two_unsigned_widths_still_widen() {
    let (_store, mut kv) = fresh_kv("widen");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    a:uint64 = 5\n    b:uint8 = 1\n    a + b -> r\n}\n",
    )
    .unwrap();
    let os = ops(&mut kv, "f");
    assert!(os.iter().any(|o| o == "add"), "should emit semantic operator add: {os:?}");
}

#[test]
fn confirmed_mixed_signs_stay_untyped() {
    let (_store, mut kv) = fresh_kv("mixed");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    a:uint64 = 5\n    b:int64 = 1\n    a + b -> r\n}\n",
    )
    .unwrap();
    let os = ops(&mut kv, "f");
    assert!(
        os.iter().any(|o| o == "add" || o == "+"),
        "should emit generic operator (add/+): {os:?}"
    );
    assert!(
        !os.iter().any(|o| o.contains('·')),
        "must not emit typed opcode: {os:?}"
    );
    assert!(
        !literal_kinds(&mut kv, "f").contains(&"uint64".to_string()),
        "must not adapt literal to unsigned on signedness conflict"
    );
}

#[test]
fn comparison_with_literal_adapts_but_negative_does_not() {
    let (_store, mut kv) = fresh_kv("cmp");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u == 1 -> r\n}\n",
    )
    .unwrap();
    assert!(
        literal_kinds(&mut kv, "f").contains(&"uint64".to_string()),
        "u == 1 should adapt to uint64"
    );

    let (_store, mut kv) = fresh_kv("cmpneg");
    compile(
        &mut kv,
        "rwfunc f() -> () {\n    u:uint64 = 5\n    u == -1 -> r\n}\n",
    )
    .unwrap();
    assert!(
        !literal_kinds(&mut kv, "f").contains(&"uint64".to_string()),
        "-1 cannot adapt to uint64 and should keep generic comparison"
    );
}
