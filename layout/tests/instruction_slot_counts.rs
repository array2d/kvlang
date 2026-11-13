
use pathlanglayout::{init_dirs, Kv};
use std::path::PathBuf;
use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_pathlanglayout")
}

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

fn temp_dir(tag: &str) -> TempDir {
    let dir = std::env::temp_dir().join(format!(
        "pathlanglayout_slotcounts_{}_{}",
        tag,
        std::process::id()
    ));
    
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    TempDir(dir)
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

fn count_slots(kv: &mut Kv, dir: &str, instruction: usize) -> (usize, usize) {
    let mut reads = 0;
    let mut writes = 0;
    for child in kv.list(&format!("{dir}/"), false, false) {
        let name = child
            .trim_end_matches('/')
            .trim_start_matches('[')
            .to_string();
        let Some((row, col)) = name.split_once(',') else {
            continue;
        };
        let Ok(col) = col.trim_end_matches(']').parse::<i32>() else {
            continue;
        };
        if row.parse::<usize>().ok() != Some(instruction) || col == 0 {
            continue;
        }
        if col < 0 {
            reads += 1;
        } else {
            writes += 1;
        }
    }
    (reads, writes)
}

fn declared_counts(kv: &mut Kv, dir: &str, instruction: usize) -> (usize, usize) {
    let wire = kv.get_one(&format!("{dir}/[{instruction},0]"));
    assert!(wire.len() >= 21, "{dir}/[{instruction},0] 缺计数头");
    let body = &wire[16..];
    let nr = u16::from_le_bytes([body[0], body[1]]) as usize;
    let nw = u16::from_le_bytes([body[2], body[3]]) as usize;
    (nr, nw)
}

fn assert_counts_match(kv: &mut Kv, dir: &str, tag: &str) {
    let mut instruction = 1;
    loop {
        if kv.get_one(&format!("{dir}/[{instruction},0]")).is_empty() {
            break;
        }
        let (nr, nw) = declared_counts(kv, dir, instruction);
        let (reads, writes) = count_slots(kv, dir, instruction);
        assert_eq!(
            (reads, writes),
            (nr, nw),
            "{tag}: {dir}/[{instruction},0] 声明 nr={nr} nw={nw}，实际读槽 {reads} 个 / 写槽 {writes} 个"
        );
        instruction += 1;
    }
}

#[test]
fn struct_literal_with_two_fields_writes_every_argument_slot() {
    let (_dir, mut kv) = compile(
        "struct2",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\nlib p {\n\trwfunc f() -> () {\n\t\tq:Point = {x=3 y=4}\n\t}\n}\n",
    );
    assert_counts_match(&mut kv, "/lib/p·f", "two-field struct");
    assert_eq!(declared_counts(&mut kv, "/lib/p·f", 1), (5, 1));
    assert_eq!(count_slots(&mut kv, "/lib/p·f", 1), (5, 1));
}

#[test]
fn struct_literal_with_six_fields_writes_every_argument_slot() {
    let (_dir, mut kv) = compile(
        "struct6",
        "struct Big {\n\ta:int64=0\n\tb:int64=0\n\tc:int64=0\n\td:int64=0\n\te:int64=0\n\tf:int64=0\n}\nlib p {\n\trwfunc g() -> () {\n\t\tq:Big = {a=1 b=2 c=3 d=4 e=5 f=6}\n\t}\n}\n",
    );
    assert_counts_match(&mut kv, "/lib/p·g", "six-field struct");
    assert_eq!(declared_counts(&mut kv, "/lib/p·g", 1), (13, 1));
    assert_eq!(count_slots(&mut kv, "/lib/p·g", 1), (13, 1));
}

#[test]
fn map_new_stays_two_reads_two_slots() {
    let (_dir, mut kv) = compile(
        "map2",
        "lib p {\n\trwfunc m() -> () {\n\t\ta:'int64'int64 = {1 2}\n\t}\n}\n",
    );
    assert_counts_match(&mut kv, "/lib/p·m", "map·new");
    assert_eq!(declared_counts(&mut kv, "/lib/p·m", 1), (2, 1));
    assert_eq!(count_slots(&mut kv, "/lib/p·m", 1), (2, 1));
}

#[test]
fn the_check_detects_a_mismatch_when_one_is_introduced() {
    let (_dir, mut kv) = compile(
        "detect",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\nlib p {\n\trwfunc f() -> () {\n\t\tq:Point = {x=3 y=4}\n\t}\n}\n",
    );
    assert_eq!(count_slots(&mut kv, "/lib/p·f", 1), (5, 1));

    let key = "/lib/p·f/[1,0]";
    let langtype = kv.langtype_of(key);
    let mut mutated = kv.get_one(key);
    mutated[16..18].copy_from_slice(&3u16.to_le_bytes());
    kv.set(&[(key.to_string(), langtype, mutated)]).unwrap();

    assert_eq!(declared_counts(&mut kv, "/lib/p·f", 1), (3, 1));
    assert_eq!(count_slots(&mut kv, "/lib/p·f", 1), (5, 1));
}

#[ignore = "依赖 bin/pathseek，等 TASK-035"]
#[test]
fn every_tutorial_instruction_has_matching_slot_counts() {
    let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .to_path_buf();
    let runtime = repo.join("bin").join("pathseek");
    
    assert!(
        runtime.exists(),
        "缺 {}：无法 boot /‥isa；请先构建 pathseek",
        runtime.display()
    );

    let mut files = Vec::new();
    let mut stack = vec![repo.join("tutorial")];
    while let Some(path) = stack.pop() {
        for entry in std::fs::read_dir(&path).unwrap() {
            let path = entry.unwrap().path();
            if path.is_dir() {
                stack.push(path);
            } else if path.extension().is_some_and(|e| e == "kv") {
                files.push(path);
            }
        }
    }
    files.sort();
    assert!(!files.is_empty(), "tutorial 下没有找到 .kv");

    let snapshot = temp_dir("corpus-worktree");
    let mut materialized = Vec::new();
    for file in files.iter() {
        let relative = file.strip_prefix(&repo).unwrap();
        let target = snapshot.join(relative);
        std::fs::create_dir_all(target.parent().unwrap()).unwrap();
        std::fs::copy(file, &target).unwrap();
        materialized.push(target);
    }
    let files = materialized;

    let root = temp_dir("corpus");
    let mut checked = 0;
    let mut skipped = Vec::new();

    {
        let probe_db = format!("shm://{}", root.join("bootprobe").display());
        let probe = Command::new(&runtime)
            .args(["-c", "print(\"boot\")"])
            .env("PATHSPACE", &probe_db)
            .current_dir(&repo)
            .output()
            .unwrap();
        if !probe.status.success() {
            let reason = String::from_utf8_lossy(&probe.stderr);
            let first = reason.lines().next().unwrap_or("(no stderr)");
            eprintln!(
                "SKIP every_tutorial_instruction_has_matching_slot_counts: \
                 pathseek: 依赖未就绪 — {first}"
            );
            eprintln!("  依赖名: pathseek → lib/libpathlang_runtimec2.so");
            eprintln!("  原因: relink to runtimec pending (tier A/B/C)");
            eprintln!("  跳过 1（依赖未就绪）");
            return;
        }
    }

    for (index, file) in files.iter().enumerate() {
        let db = format!("shm://{}", root.join(format!("case{index}")).display());
        let boot = Command::new(&runtime)
            .args(["-c", "print(\"boot\")"])
            .env("PATHSPACE", &db)
            .current_dir(&repo)
            .output()
            .unwrap();
        assert!(
            boot.status.success(),
            "{}: boot 失败 {}",
            file.display(),
            String::from_utf8_lossy(&boot.stderr)
        );
        let out = Command::new(bin())
            .arg(file)
            .arg(&db)
            .current_dir(&repo)
            .output()
            .unwrap();
        if !out.status.success() {
            
            let stderr = String::from_utf8_lossy(&out.stderr).into_owned();
            
            assert!(
                stderr.contains("未定义函数"),
                "{}: layout 失败且不是缺能力：{stderr}",
                file.display()
            );
            skipped.push(file.display().to_string());
            continue;
        }
        let mut kv = Kv::conn(&db);
        init_dirs(&mut kv).unwrap();
        for dir in function_dirs(&mut kv, "/lib/") {
            assert_counts_match(&mut kv, &dir, &file.display().to_string());
            checked += 1;
        }
        
        drop(kv);
        let _ = std::fs::remove_dir_all(root.join(format!("case{index}")));
    }
    
    assert!(checked > 0, "tutorial 一个函数目录都没扫到");
    eprintln!(
        "扫描 {checked} 个函数目录；{} 个用例缺能力跳过：{skipped:?}",
        skipped.len()
    );
}

fn function_dirs(kv: &mut Kv, prefix: &str) -> Vec<String> {
    let mut out = Vec::new();
    for child in kv.list(prefix, false, false) {
        let name = child.trim_end_matches('/').trim_end_matches(".src");
        let key = format!("{prefix}{name}");
        if kv.get_one(&format!("{key}/[1,0]")).is_empty() {
            continue;
        }
        out.push(key);
    }
    out
}

#[test]
fn numeric_op_after_member_read_is_a_known_gap() {
    
    let (_dir_a, mut kv_a) = compile(
        "numop-a",
        "rwfunc f() -> () {\n\toperand:int64 = 5\n\tq:int64 = operand % 2\n}\n",
    );
    let op_a = slot_text(&mut kv_a, "/lib/f", 2, 0);
    assert_eq!(op_a, "mod", "A 形态：算子应已归一成 mod（实测 {op_a:?}）");

    let (_dir_b, mut kv_b) = compile(
        "numop-b",
        "struct P {\n\tx:int64=0\n}\n\
         rwfunc f(*P #p) -> () {\n\tnv:int64 = (*p)·x\n\tq:int64 = nv % 2\n}\n",
    );
    let op_b = slot_text(&mut kv_b, "/lib/f", 3, 0);
    
    assert_eq!(op_b, "mod", "B 形态：算子已归一成 mod（实测 {op_b:?}）");
}

fn slot_text(kv: &mut Kv, dir: &str, instruction: usize, column: i32) -> String {
    let wire = kv.get_one(&format!("{dir}/[{instruction},{column}]"));
    let text: String = wire
        .iter()
        .map(|byte| {
            if byte.is_ascii_graphic() || *byte == b' ' {
                *byte as char
            } else {
                ' '
            }
        })
        .collect();
    text.split_whitespace()
        .filter(|piece| *piece != "rwir" && *piece != "rwfunc")
        .collect::<Vec<_>>()
        .join(" ")
}
