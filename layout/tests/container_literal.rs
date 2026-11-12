
use kvlanglayout::{init_dirs, Kv};
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
    env!("CARGO_BIN_EXE_kvlanglayout")
}

fn temp_dir(tag: &str) -> TempDir {
    let dir = std::env::temp_dir().join(format!(
        "kvlanglayout_container_literal_{}_{}",
        tag,
        std::process::id()
    ));
    
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    TempDir(dir)
}

fn layout(tag: &str, src: &str) -> (TempDir, Result<std::process::Output, String>) {
    let dir = temp_dir(tag);
    let file = dir.join("p.kv");
    std::fs::write(&file, src).unwrap();
    let db = format!("shm://{}", dir.join("db").display());
    let out = Command::new(bin()).arg(&file).arg(&db).output().unwrap();
    (dir, Ok(out))
}

fn body<'a>(wire: &'a [u8]) -> &'a [u8] {
    let len = u64::from_le_bytes(wire[0..8].try_into().unwrap()) as usize;
    &wire[16..16 + len]
}

fn opcode(kv: &mut Kv, dir: &str, instruction: usize) -> String {
    let wire = kv.get_one(&format!("{dir}/[{instruction},0]"));
    assert!(!wire.is_empty(), "{dir}/[{instruction},0] 不存在");
    let body = body(&wire);
    String::from_utf8_lossy(&body[5..]).into_owned()
}

fn write_type(kv: &mut Kv, dir: &str, instruction: usize) -> String {
    let wire = kv.get_one(&format!("{dir}/[{instruction},1]"));
    assert!(!wire.is_empty(), "{dir}/[{instruction},1] 不存在");
    let body = body(&wire);
    let text = String::from_utf8_lossy(&body[5..]).into_owned();
    text.rsplit_once('\0')
        .map(|(_, ty)| ty.to_string())
        .unwrap_or_default()
}

fn connect(dir: &TempDir) -> Kv {
    let mut kv = Kv::conn(&format!("shm://{}", dir.join("db").display()));
    init_dirs(&mut kv).unwrap();
    kv
}

#[test]
fn empty_map_literal_is_map_new_with_annotated_type() {
    let (dir, out) = layout(
        "empty",
        "lib p {\n\trwfunc f() -> () {\n\t\tm:'[int64]'[]char/utf8 = {}\n\t}\n}\n",
    );
    let out = out.unwrap();
    assert!(
        out.status.success(),
        "layout failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut kv = connect(&dir);
    let dir_key = "/lib/p·f";
    assert_eq!(opcode(&mut kv, dir_key, 1), "map·new");
    let (nr, nw) = declared(&mut kv, dir_key, 1);
    assert_eq!((nr, nw), (0, 1), "空 map 是零读参、一写参");
    assert_eq!(
        write_type(&mut kv, dir_key, 1),
        "'[int64]'[]char/utf8",
        "写槽类型必须来自声明处标注"
    );
}

#[test]
fn non_empty_map_literal_is_map_new() {
    let (dir, out) = layout(
        "nonempty",
        "lib p {\n\trwfunc f() -> () {\n\t\tm:'int64'int64 = {1 2 3}\n\t}\n}\n",
    );
    assert!(out.unwrap().status.success());
    let mut kv = connect(&dir);
    assert_eq!(opcode(&mut kv, "/lib/p·f", 1), "map·new");
    assert_eq!(declared(&mut kv, "/lib/p·f", 1), (3, 1));
}

#[test]
fn struct_literal_is_struct_new_not_map() {
    let (dir, out) = layout(
        "struct",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\nlib p {\n\trwfunc f() -> () {\n\t\tq:Point = {x=3 y=4}\n\t}\n}\n",
    );
    assert!(out.unwrap().status.success());
    let mut kv = connect(&dir);
    assert_eq!(opcode(&mut kv, "/lib/p·f", 1), "struct·new");
    
    assert_eq!(declared(&mut kv, "/lib/p·f", 1), (5, 1));
}

#[test]
fn unannotated_container_literal_is_rejected() {
    let (_dir, out) = layout(
        "noannot",
        "lib p {\n\trwfunc f() -> () {\n\t\tm = {}\n\t}\n}\n",
    );
    let out = out.unwrap();
    assert!(!out.status.success(), "无标注的容器字面量必须报错");
    let stderr = String::from_utf8_lossy(&out.stderr);
    assert!(
        stderr.contains("needs a map langtype"),
        "错误消息要指认缺类型，实际：{stderr}"
    );
}

#[test]
fn no_literal_form_emits_obj() {
    for (tag, body) in [
        ("obj-empty", "m:'[int64]'int64 = {}\n\t"),
        ("obj-nonempty", "m:'int64'int64 = {1 2}\n\t"),
    ] {
        let src = format!("lib p {{\n\trwfunc f() -> () {{\n\t\t{body}}}\n}}\n");
        let (dir, out) = layout(tag, &src);
        let out = out.unwrap();
        assert!(
            out.status.success(),
            "{tag}: {}",
            String::from_utf8_lossy(&out.stderr)
        );
        let mut kv = connect(&dir);
        let mut instruction = 1;
        loop {
            let wire = kv.get_one(&format!("/lib/p·f/[{instruction},0]"));
            if wire.is_empty() {
                break;
            }
            assert_ne!(
                opcode(&mut kv, "/lib/p·f", instruction),
                "obj",
                "{tag}: 第 {instruction} 条指令仍在发 obj"
            );
            instruction += 1;
        }
    }
}

#[test]
fn signature_declaration_supplies_the_map_type() {
    let (dir, out) = layout(
        "sig",
        "rwfunc build() -> (*'[]char/utf8'int64 #m) {\n\t{n=42; x=7} -> *m\n}\n",
    );
    let out = out.unwrap();
    assert!(
        out.status.success(),
        "具名 map 字面量是规格允许的：{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut kv = connect(&dir);
    
    assert_eq!(declared(&mut kv, "/lib/build", 1), (4, 1));
}

fn declared(kv: &mut Kv, dir: &str, instruction: usize) -> (usize, usize) {
    let wire = kv.get_one(&format!("{dir}/[{instruction},0]"));
    let body = body(&wire);
    (
        u16::from_le_bytes([body[0], body[1]]) as usize,
        u16::from_le_bytes([body[2], body[3]]) as usize,
    )
}
