
use std::collections::BTreeMap;
use std::sync::atomic::{AtomicUsize, Ordering};

use pathlanglayout::{compile, init_dirs, kvkind, parser, vet, Kv};

static SEQ: AtomicUsize = AtomicUsize::new(0);

struct Store {
    kv: Kv,
    dir: String,
}

impl Drop for Store {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.dir);
    }
}

fn store() -> Store {
    let dir = format!(
        "/tmp/pathlanglayout_robust_{}_{}",
        std::process::id(),
        SEQ.fetch_add(1, Ordering::SeqCst)
    );
    let _ = std::fs::remove_dir_all(&dir);
    let mut kv = Kv::conn(&format!("shm://{dir}"));
    init_dirs(&mut kv).unwrap();
    kv.set(&[(
        "/lib/println".to_string(),
        kvkind::KIND_DEF_RWIR.to_string(),
        kvkind::new_defrwir(1, 0, true).unwrap(),
    )])
    .unwrap();
    Store { kv, dir }
}

fn errors(src: &str) -> Vec<(i32, i32, String)> {
    let (_, d) = parser::parse_code(src).unwrap();
    d.into_iter()
        .filter(|d| !d.warn && !d.info)
        .map(|d| (d.pos.line, d.pos.col, d.message))
        .collect()
}

fn has_error_at(src: &str, line: i32, col: i32, needle: &str) -> bool {
    errors(src)
        .iter()
        .any(|(l, c, m)| *l == line && *c == col && m.contains(needle))
}

fn body(data: &[u8]) -> Vec<u8> {
    kvkind::body(data).to_vec()
}

fn slot(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let lt = kv.langtype_of(key);
    if kvkind::kind(&v, &lt) == "int64" {
        return kvkind::display(&v, &lt)
            .rsplit(':')
            .next()
            .unwrap_or("")
            .to_string();
    }
    let b = body(&v);
    let raw = String::from_utf8_lossy(&b[5.min(b.len())..]);
    raw.split('\0').next().unwrap_or("").to_string()
}

fn plane(kv: &mut Kv, func: &str) -> BTreeMap<i32, (String, Vec<String>)> {
    let mut out = BTreeMap::new();
    for row in 1.. {
        let op_key = format!("/lib/{func}/[{row},0]");
        if kv.get_one(&op_key).is_empty() {
            break;
        }
        let op = slot(kv, &op_key);
        let mut reads = Vec::new();
        for j in 1.. {
            let k = format!("/lib/{func}/[{row},-{j}]");
            if kv.get_one(&k).is_empty() {
                break;
            }
            reads.push(slot(kv, &k));
        }
        out.insert(row, (op, reads));
    }
    out
}

#[test]
fn break_continue_outside_loop_is_positioned_error() {
    let src = "rwfunc test() -> () {\n\tprintln(\"before\")\n\tbreak\n}\n";
    assert!(
        has_error_at(src, 3, 2, "break outside loop"),
        "{:?}",
        errors(src)
    );
    let src2 = "lib p {\n\tcontinue\n}\n";
    assert!(
        has_error_at(src2, 2, 2, "continue outside loop"),
        "{:?}",
        errors(src2)
    );
    assert!(vet(src).is_err());
    let mut s = store();
    assert!(compile(&mut s.kv, src).is_err());
    assert!(
        s.kv.get_one("/lib/test/[0,0]").is_empty(),
        "rejected program must not be written"
    );
    
    let ok = "rwfunc t(*int64 #n) -> () {\n\ti = 0\n\twhile (i < *n) {\n\t\tif (i == 2) { break }\n\t\ti + 1 -> i\n\t}\n}\n";
    assert!(errors(ok).is_empty(), "{:?}", errors(ok));
    assert!(vet(ok).is_ok());
}

#[test]
fn user_label_errors_are_positioned() {
    let src = "rwfunc f() -> () {\n\tL: {\n\t\treturn\n\t}\n\tL: {\n\t\treturn\n\t}\n\tgoto(M)\n\tbr(1, L)\n}\n";
    assert!(
        has_error_at(src, 5, 2, "duplicate block label \"L\""),
        "{:?}",
        errors(src)
    );
    assert!(
        has_error_at(src, 8, 2, "unknown label \"M\""),
        "{:?}",
        errors(src)
    );
    assert!(has_error_at(src, 9, 2, "br expects 3"), "{:?}", errors(src));
    assert!(vet(src).is_err());
    
    let other =
        "rwfunc a() -> () {\n\tL: {\n\t\treturn\n\t}\n}\nrwfunc b() -> () {\n\tgoto(L)\n}\n";
    assert!(
        has_error_at(other, 7, 2, "unknown label \"L\""),
        "{:?}",
        errors(other)
    );
}

#[test]
fn targeted_label_is_entered_by_fallthrough() {
    let top = "rwfunc f(*int64 #c) -> (*int64 #r) {\n\tif (*c == 1) {\n\t\tgoto(L)\n\t}\n\t1 -> *r\n\tL: {\n\t\t*r + 10 -> *r\n\t\treturn\n\t}\n}\n";
    assert!(vet(top).is_ok());
    let mut s = store();
    compile(&mut s.kv, top).unwrap();
    
    let lb = body(&s.kv.get_one("/lib/f/‥labels/L"));
    let l = i64::from_le_bytes(lb[..8].try_into().unwrap()).to_string();
    let p = plane(&mut s.kv, "f");
    let (row, _) = p
        .iter()
        .find(|(_, (op, reads))| op == "=" && reads == &vec!["1".to_string()])
        .expect("`1 -> r` missing");
    let next = &p[&(row + 1)];
    assert_eq!(next.0, "goto", "after `1 -> r` must fall into L: {p:?}");
    assert_eq!(next.1, vec![l], "goto target must be L's irseq");
}

#[test]
fn vet_rejects_what_compile_rejects() {
    let bad = [
        "rwfunc f() -> () {\n\tx:int8 = 300\n}\n",
        "rwfunc f(*int64 #a) -> () {\n\tx:int8 = 300\n}\n",
        "struct P {\n\tx:int8=300\n}\n",
        "struct P {\n\tx:int64=\"a\"\n}\n",
    ];
    for src in bad {
        assert!(vet(src).is_err(), "vet accepted:\n{src}");
        let mut s = store();
        assert!(compile(&mut s.kv, src).is_err(), "compile accepted:\n{src}");
    }
    let good = "rwfunc f(*int64 #a) -> (*int64 #r) {\n\tadd(*a, 1) -> *r\n}\n";
    assert!(vet(good).is_ok());
    let mut s = store();
    compile(&mut s.kv, good).unwrap();
}

#[test]
fn malformed_write_slots_are_errors_not_panics() {
    for (src, line, col, needle) in [
        (
            "rwfunc f() -> () {\n\t4 -> a[\n}\n",
            2,
            9,
            "unterminated `[`",
        ),
        (
            "rwfunc f() -> () {\n\ti = 1\n\t2 -> \"/tmp/s\"·*i+1\n}\n",
            3,
            19,
            "single name",
        ),
        (
            "rwfunc f() -> () {\n\t3 -> m·*\n}\n",
            2,
            10,
            "expected a name after `·*`",
        ),
        (
            "rwfunc f() -> () {\n\tk = 1\n\tm·*(k) -> z\n}\n",
            3,
            5,
            "expected a name after `·*`",
        ),
    ] {
        assert!(
            has_error_at(src, line, col, needle),
            "{src:?}: {:?}",
            errors(src)
        );
        assert!(vet(src).is_err());
    }
    
    let a = "rwfunc f() -> () {\n\t/tmp/s:[int64]·int64 = {}\n\t1 -> \"/tmp/s\"·*0\n}\n";
    let b = "rwfunc f() -> () {\n\t/tmp/s:[int64]·int64 = {}\n\t1 -> \"/tmp/s\"·0\n}\n";
    let (mut sa, mut sb) = (store(), store());
    compile(&mut sa.kv, a).unwrap();
    compile(&mut sb.kv, b).unwrap();
    assert_eq!(slot(&mut sa.kv, "/lib/f/[2,1]"), "/tmp/s·0");
    assert_eq!(plane(&mut sa.kv, "f"), plane(&mut sb.kv, "f"));
}

#[test]
fn non_expression_in_list_does_not_hang() {
    for src in [
        "rwfunc t() -> () {\n\tprintln(q=1)\n}\n",
        "rwfunc t() -> () {\n\tx = f(a = 1)\n}\n",
        "rwfunc t() -> () {\n\tx = json·to(a = 1)\n}\n",
        "rwfunc t() -> () {\n\tx = [1, =]\n}\n",
        "rwfunc t() -> () {\n\tx = a[=]\n}\n",
    ] {
        let (tx, rx) = std::sync::mpsc::channel();
        let owned = src.to_string();
        std::thread::spawn(move || {
            let _ = tx.send(vet(&owned).is_err());
        });
        let rejected = rx
            .recv_timeout(std::time::Duration::from_secs(10))
            .unwrap_or_else(|_| panic!("vet hung on {src:?}"));
        assert!(rejected, "{src:?}");
    }
}

#[test]
fn integral_float_default_for_int_field_is_kept() {
    let mut s = store();
    compile(&mut s.kv, "struct P {\n\tx:int64=3.0\n\ty:uint8=255\n}\n").unwrap();
    assert_eq!(
        kvkind::display(&s.kv.get_one("/lib/P·x"), "int64")
            .rsplit(':')
            .next(),
        Some("3")
    );
    let y = s.kv.get_one("/lib/P·y");
    assert_eq!(body(&y), vec![255u8]);
}

#[test]
fn failed_compile_keeps_previous_function_intact() {
    let mut s = store();
    compile(&mut s.kv, "rwfunc g() -> () {\n\tprintln(1)\n}\n").unwrap();
    let before = s.kv.get_one("/lib/g/[1,0]");
    assert!(!before.is_empty());
    let err = compile(&mut s.kv, "rwfunc g() -> () {\n\tx:int8 = 300\n}\n").unwrap_err();
    assert!(err.contains("refusing to load"), "{err}");
    assert_eq!(
        s.kv.get_one("/lib/g/[1,0]"),
        before,
        "old body must survive a failed re-layout"
    );
    let src = slot(&mut s.kv, "/lib/g.src");
    assert!(src.contains("println"), "old .src must survive: {src:?}");
    let err = compile(
        &mut s.kv,
        "rwfunc h(*int64 #a) -> () {\n\tx:int8 = 300\n}\n",
    )
    .unwrap_err();
    
    assert!(err.contains("refusing to load"), "{err}");
    assert!(
        s.kv.get_one("/lib/h/[0,0]").is_empty(),
        "no half-written signature"
    );
    assert!(s.kv.get_one("/lib/h.src").is_empty());
    let err = compile(
        &mut s.kv,
        "rwfunc g() -> () {\n\tprintln(2)\n}\nrwfunc h() -> () {\n\tx:int8 = 300\n}\n",
    )
    .unwrap_err();
    assert!(err.contains("refusing to load"), "{err}");
    assert_eq!(
        s.kv.get_one("/lib/g/[1,0]"),
        before,
        "earlier function must not be written either"
    );
}

#[test]
fn cli_reports_errors_without_panicking() {
    let dir = std::env::temp_dir().join(format!("pathlanglayout_robust_cli_{}", std::process::id()));
    
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let f = dir.join("brk.kv");
    std::fs::write(
        &f,
        "rwfunc test() -> () {\n\tprintln(\"before\")\n\tbreak\n}\n",
    )
    .unwrap();
    let bin = env!("CARGO_BIN_EXE_pathlanglayout");
    let dsn = format!("shm://{}", dir.join("db").display());
    for args in [
        vec!["vet".to_string(), f.display().to_string()],
        vec![f.display().to_string(), dsn.clone()],
        vec![
            "printlib".to_string(),
            f.display().to_string(),
            "/lib".to_string(),
            dsn.clone(),
        ],
    ] {
        let out = std::process::Command::new(bin)
            .args(&args)
            .output()
            .unwrap();
        let err = String::from_utf8_lossy(&out.stderr);
        assert_eq!(out.status.code(), Some(1), "{args:?}: {err}");
        assert!(err.contains("3:2: break outside loop"), "{args:?}: {err}");
        assert!(!err.contains("panicked"), "{args:?}: {err}");
    }
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn truncated_corpus_never_panics() {
    let root = concat!(env!("CARGO_MANIFEST_DIR"), "/../tutorial");
    let mut files = Vec::new();
    let mut stack = vec![std::path::PathBuf::from(root)];
    while let Some(d) = stack.pop() {
        for e in std::fs::read_dir(&d).unwrap() {
            let p = e.unwrap().path();
            if p.is_dir() {
                stack.push(p);
            } else if p.extension().is_some_and(|x| x == "kv") {
                files.push(p);
            }
        }
    }
    files.sort();
    let prev = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let mut bad = Vec::new();
    for f in &files {
        let src = std::fs::read_to_string(f).unwrap();
        let step = (src.len() / 12).max(1);
        for cut in (step..src.len()).step_by(step) {
            if !src.is_char_boundary(cut) {
                continue;
            }
            let s = &src[..cut];
            if std::panic::catch_unwind(|| {
                let _ = vet(s);
                let _ = pathlanglayout::format(s);
            })
            .is_err()
            {
                bad.push(format!("{}@{cut}", f.display()));
            }
        }
    }
    std::panic::set_hook(prev);
    assert!(bad.is_empty(), "panicked on: {bad:?}");
}

#[test]
fn format_round_trips_member_reads_and_empty_kindexp() {
    let src = "rwfunc f(*int64 #a, *int64 #n) -> (*int64 #r) {\n\tm:[]char/utf8·int64 = {}\n\tk = \"x\"\n\tm·x = 1\n\tm·x -> v1\n\tm·*k -> v2\n\t\"/tmp/up\"·*n -> v3\n\t\"/tmp/up\"·0 -> v4\n\tm·\"a b\" -> v5\n\tm·\"if\" -> v6\n\tv1 + m·x -> *r\n}\n";
    let once = pathlanglayout::format(src).unwrap();
    assert!(
        !once.contains("=("),
        "member read rendered as a call:\n{once}"
    );
    assert!(once.contains("f(*int64 #a, *int64 #n)"), "{once}");
    assert!(
        errors(&once).is_empty(),
        "formatted output must parse:\n{once}\n{:?}",
        errors(&once)
    );
    assert_eq!(
        pathlanglayout::format(&once).unwrap(),
        once,
        "format must be idempotent"
    );
    let (mut a, mut b) = (store(), store());
    compile(&mut a.kv, src).unwrap();
    compile(&mut b.kv, &once).unwrap();
    assert_eq!(plane(&mut a.kv, "f"), plane(&mut b.kv, "f"));
}

#[test]
fn deref_of_expression_does_not_panic() {
    let src = "rwfunc t(*int64 #a) -> () {\n\t*(*a + 1) -> x\n\t(*a)·b -> y\n}\n";
    assert!(vet(src).is_ok());
    let mut s = store();
    compile(&mut s.kv, src).unwrap();
    
    let p = plane(&mut s.kv, "t");
    assert_eq!(p[&1], ("=".to_string(), vec!["*[0,-1]".to_string()]));
    assert_eq!(p[&2].0, "add");
    assert_eq!(p[&3], ("=".to_string(), vec!["*_1".to_string()]));
    let src2 = "rwfunc f(*int64 #a) -> (*int64 #r) { *a -> *r }\nrwfunc t(*int64 #a) -> () {\n\tx = *f(*a)\n}\n";
    assert!(vet(src2).is_ok());
    compile(&mut store().kv, src2).unwrap();
}

#[test]
fn rwfunc_signature_rejects_union_but_rwir_declaration_keeps_it() {
    let single = "rwfunc f(int64 #a) -> (int64 #r) {\n\t*a -> #r\n}\n";
    assert!(
        errors(single)
            .iter()
            .all(|(_, _, m)| !m.contains("single type")),
        "{:?}",
        errors(single)
    );

    let param_union = "rwfunc f(int64|float64 #a) -> (int64 #r) {\n\t*a -> #r\n}\n";
    assert!(
        errors(param_union)
            .iter()
            .any(|(_, _, m)| m.contains("rwfunc parameter must be a single type")),
        "{:?}",
        errors(param_union)
    );

    let return_union = "rwfunc f(int64 #a) -> (int64|float64 #r) {\n\t*a -> #r\n}\n";
    assert!(
        errors(return_union)
            .iter()
            .any(|(_, _, m)| m.contains("rwfunc return type must be a single type")),
        "{:?}",
        errors(return_union)
    );

    let rwir_union = "rwir demo·pick(*int64|float64 #a) -> (*int64|float64 #r)\n";
    assert!(
        errors(rwir_union)
            .iter()
            .all(|(_, _, m)| !m.contains("single type")),
        "{:?}",
        errors(rwir_union)
    );
}

#[test]
fn variadic_parameter_accepts_any_arity() {
    for arity in [0usize, 1, 4] {
        let args = vec!["1"; arity].join(", ");
        let src =
            format!("rwfunc f(*int64... #x) -> () {{\n}}\nrwfunc g() -> () {{\n\tf({args})\n}}\n");
        assert!(vet(&src).is_ok(), "arity {arity}: {:?}", errors(&src));
    }
}

#[test]
fn variadic_parameter_still_checks_type() {
    let bad = "rwfunc f(*[]char/utf32 #s...) -> () {\n}\nrwfunc g() -> () {\n\tf(1, 2)\n}\n";
    
    assert!(vet(bad).is_err(), "变参位把 int64 当字符串收了");

    let ok =
        "rwfunc f(*[]char/utf32 #s...) -> () {\n}\nrwfunc g() -> () {\n\tf(\"a\", \"b\", \"c\")\n}\n";
    assert!(vet(ok).is_ok(), "{:?}", errors(ok));
}

#[test]
fn variadic_marker_must_be_last() {
    let src = "rwfunc f(*int64... #a, *int64 #b) -> () {\n}\n";
    assert!(
        !errors(src).is_empty(),
        "非末位的 `...` 应被拒绝，实际无诊断"
    );
}
