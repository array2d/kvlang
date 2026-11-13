
use pathlanglayout::{init_dirs, kvkind, Kv};
use std::path::PathBuf;
use std::process::Command;

struct TempDir(PathBuf);

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

impl TempDir {
    fn join<P: AsRef<std::path::Path>>(&self, p: P) -> PathBuf {
        self.0.join(p)
    }
}

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_pathlanglayout")
}

fn temp_dir(tag: &str) -> TempDir {
    let dir = std::env::temp_dir().join(format!(
        "pathlanglayout_slottypes_{}_{}",
        tag,
        std::process::id()
    ));
    
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    TempDir(dir)
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let b = kvkind::body(&v);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

fn read_slot_type(kv: &mut Kv, key: &str) -> String {
    let text = slot_text(kv, key);
    match text.rsplit_once('\0') {
        Some((_, ty)) => ty.to_string(),
        None => kv.langtype_of(key),
    }
}

fn compile(tag: &str, src: &str) -> (TempDir, Kv) {
    let dir = temp_dir(tag);
    let file = dir.join("p.kv");
    std::fs::write(&file, src).unwrap();
    let db = format!("shm://{}", dir.join("db").display());
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    assert!(
        out.status.success(),
        "layout failed ({tag}): {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut kv = Kv::conn(&db);
    init_dirs(&mut kv).unwrap();
    (dir, kv)
}

#[test]
fn mixed_precision_params_keep_their_own_types() {
    let (_dir, mut kv) = compile(
        "mixed",
        "lib p {\n\trwfunc f(*int64 #a, *float64 #b) -> (*float64 #r) {\n\t\t*a + *b -> *r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv, "/lib/p·f/[1,-1]"), "int64");
    assert_eq!(read_slot_type(&mut kv, "/lib/p·f/[2,-1]"), "float64");
}

#[test]
fn union_declaration_with_single_assignment_kind_is_narrowed() {
    let (_dir, mut kv) = compile(
        "narrow",
        "lib p {\n\trwfunc h() -> (*int64 #r) {\n\t\ty:int64|float64 = 1\n\t\ty + 1 -> *r\n\t}\n}\n",
    );
    assert_eq!(read_slot_type(&mut kv, "/lib/p·h/[2,-1]"), "int64");
    
    let (_dir2, mut kv2) = compile(
        "loop",
        "lib p {\n\trwfunc s(*int64 #n) -> () {\n\t\ttotal:int64|float64 = 0\n\t\ti = 0\n\t\twhile (i < *n) {\n\t\t\ttotal = total + i\n\t\t\ti = i + 1\n\t\t}\n\t}\n}\n",
    );
    
    assert_eq!(read_slot_type(&mut kv2, "/lib/p·s/[7,-1]"), "int64");
}

#[test]
fn unresolvable_union_is_preserved() {
    
    let (_dir2, mut kv2) = compile(
        "branch",
        "lib p {\n\trwfunc m(*bool #c) -> (*float64 #r) {\n\t\tx:int64|float64 = 1\n\t\tif (*c) {\n\t\t\tx = 2.5\n\t\t}\n\t\tx + 1.0 -> *r\n\t}\n}\n",
    );
    
    assert_eq!(read_slot_type(&mut kv2, "/lib/p·m/[8,-1]"), "int64|float64");
}

#[test]
fn narrowing_never_invents_a_kind_outside_the_union() {
    let (_dir, mut kv) = compile(
        "outside",
        "lib p {\n\trwfunc f() -> (*int8 #r) {\n\t\ty:int8|int16 = 1\n\t\ty + int8(1) -> *r\n\t}\n}\n",
    );
    
    let ty = read_slot_type(&mut kv, "/lib/p·f/[3,-1]");
    assert!(
        ty == "int8|int16" || ty == "int8",
        "只允许并集成员，实际 {ty:?}"
    );
    assert!(!ty.contains("int64"), "不得把并集外的 int64 写上去：{ty:?}");
}
