//! ISA declarations：native opcode 与扩展同形，统一落在
//! `/lib/<op>` 的 `def rwir` 明文节点；layout 只写/读这份声明，
//! 不再消费 runtime 的 `/‥isa/‥core` manifest。

use crate::ffi::{self, Kv};
use crate::{keytree, kvkind};
use std::collections::{BTreeMap, BTreeSet, HashMap};

#[derive(Clone, Debug)]
pub struct Entry {
    pub opcode: String,
    pub nr: u32,
    pub nw: u32,
    pub dynamic: bool,
    pub rp: Vec<String>,
    pub wp: Vec<String>,
    pub storetype: i32,
    pub headlen: i32,
    pub width: i32,
    pub class: String,
    pub line: String,
}

impl Entry {
    pub fn concrete(&self, i: usize, write: bool) -> Option<&str> {
        let t = if write {
            self.wp.get(i)
        } else {
            self.rp.get(i)
        }?;
        if t == "any" || t.ends_with("...") {
            None
        } else {
            Some(t.as_str())
        }
    }
}

pub struct Isa {
    pub entries: HashMap<String, Entry>,
    pub folded: BTreeMap<String, String>,
}

/// native ISA 作者清单：只供 layout 生成 `/‥isa` 声明；runtime 不读。
const NATIVE: &str = include_str!("../tests/fixtures/isa_native.txt");

fn parse_native() -> Result<Vec<Entry>, String> {
    let mut out = Vec::new();
    for (n, line) in NATIVE.lines().enumerate() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if line.starts_with("ver\t") || line.starts_with("count\t") || line.starts_with("hash\t") {
            continue;
        }
        let cols: Vec<&str> = line.split('\t').collect();
        if cols.len() < 6 {
            return Err(format!("native ISA record {} field count", n + 1));
        }
        // 死刑：kvspace·get/set 不再发射（成员读写走 =，创建走 {t}·new）。
        if cols[0] == "kvspace·get" || cols[0] == "kvspace·set" {
            continue;
        }
        let nr: u32 = cols[1].parse().map_err(|_| "native ISA nr".to_string())?;
        let nw: u32 = cols[2].parse().map_err(|_| "native ISA nw".to_string())?;
        let base = 4 + nr as usize + nw as usize;
        if cols.len() < base {
            return Err(format!("native ISA record {} arity", n + 1));
        }
        out.push(Entry {
            opcode: cols[0].to_string(),
            nr,
            nw,
            dynamic: cols[3] == "1",
            rp: cols[4..4 + nr as usize]
                .iter()
                .map(|s| (*s).to_string())
                .collect(),
            wp: cols[4 + nr as usize..base]
                .iter()
                .map(|s| (*s).to_string())
                .collect(),
            storetype: cols.get(base).and_then(|s| s.parse().ok()).unwrap_or(-1),
            headlen: cols
                .get(base + 1)
                .and_then(|s| s.parse().ok())
                .unwrap_or(-1),
            width: cols
                .get(base + 2)
                .and_then(|s| s.parse().ok())
                .unwrap_or(-1),
            class: "core".to_string(),
            line: line.to_string(),
        });
    }
    Ok(out)
}

fn wire_type(t: &str) -> &str {
    if t == "any" || t.ends_with("...") {
        "*"
    } else {
        t
    }
}

fn read_def(kv: &mut Kv, op: &str, class: &str) -> Option<Entry> {
    let key = keytree::isa_op(op);
    let d = kv.get_one(&key);
    if kvkind::kind(&d, &kv.langtype_of(&key)) != kvkind::KIND_DEF_RWIR {
        return None;
    }
    let b = ffi::xh_body_slice(&d);
    if b.len() < 5 {
        return None;
    }
    let nr = u16::from_le_bytes([b[0], b[1]]) as usize;
    let nw = u16::from_le_bytes([b[2], b[3]]) as usize;
    let mut rp = Vec::with_capacity(nr);
    let mut wp = Vec::with_capacity(nw);
    for j in 0..nr + nw {
        let x = if j < nr {
            -(j as i64 + 1)
        } else {
            (j - nr) as i64 + 1
        };
        let p = kv.get_one(&format!("{}/[0,{}]", keytree::isa_op(op), x));
        let mut ty = String::from_utf8_lossy(ffi::xh_body_slice(&p)).into_owned();
        if ty == "*" {
            ty = "any".to_string();
        }
        if j < nr {
            rp.push(ty);
        } else {
            wp.push(ty);
        }
    }
    Some(Entry {
        opcode: op.to_string(),
        nr: nr as u32,
        nw: nw as u32,
        dynamic: b[4] != 0,
        rp,
        wp,
        storetype: -1,
        headlen: -1,
        width: -1,
        class: class.to_string(),
        line: String::new(),
    })
}

fn ensure_native(kv: &mut Kv, e: &Entry) -> Result<(), String> {
    let key = keytree::isa_op(&e.opcode);
    let cur = kv.get_one(&key);
    let lt = kv.langtype_of(&key);
    let k = kvkind::kind(&cur, &lt);
    if k == kvkind::KIND_DEF_RWIR {
        return Ok(());
    }
    if !cur.is_empty() {
        return Err(format!(
            "ISA: native opcode {} is already defined as {k}",
            e.opcode
        ));
    }
    let mut pairs: Vec<(String, String, Vec<u8>)> = vec![(
        key.clone(),
        kvkind::KIND_DEF_RWIR.to_string(),
        kvkind::new_defrwir(e.nr as i32, e.nw as i32, e.dynamic),
    )];
    for (j, t) in e.rp.iter().enumerate() {
        pairs.push((
            format!("{key}/[0,-{}]", j + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_langtype(wire_type(t)),
        ));
    }
    for (j, t) in e.wp.iter().enumerate() {
        pairs.push((
            format!("{key}/[0,{}]", j + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_langtype(wire_type(t)),
        ));
    }
    kv.set(&pairs)
        .map_err(|err| format!("ISA: write native {}: {err}", e.opcode))
}

impl Isa {
    /// 写/读 native 声明；native 与扩展同形，均以 `/lib/<op>` 为唯一来源。
    pub fn load(kv: &mut Kv) -> Result<Isa, String> {
        let native = parse_native()?;
        let mut folded = BTreeMap::new();
        for e in &native {
            let path = keytree::isa_op(&e.opcode);
            if let Some(prev) = folded.insert(path.clone(), e.opcode.clone()) {
                return Err(format!(
                    "ISA op path conflict: {prev} and {} -> {path}",
                    e.opcode
                ));
            }
        }
        let _ = kv.mkindex("/‥isa/");
        for e in &native {
            ensure_native(kv, e)?;
        }
        let mut entries = HashMap::new();
        for e in native {
            let got = read_def(kv, &e.opcode, "core").unwrap_or(e);
            entries.insert(got.opcode.clone(), got);
        }
        Ok(Isa { entries, folded })
    }

    pub fn is_native(&self, op: &str) -> bool {
        self.entries.get(op).is_some_and(|e| e.class == "core")
    }

    /// 把本次函数实际发射、尚未加载的 opcode 从 `/isa` 读入。
    pub fn ensure_ops(&mut self, kv: &mut Kv, ops: &BTreeSet<String>) -> Result<(), String> {
        for op in ops {
            if self.entries.contains_key(op) {
                continue;
            }
            let path = keytree::isa_op(op);
            if let Some(prev) = self.folded.insert(path.clone(), op.clone()) {
                if prev != *op {
                    return Err(format!("ISA op path conflict: {prev} and {op} -> {path}"));
                }
            }
            if let Some(e) = read_def(kv, op, "ext") {
                self.entries.insert(op.clone(), e);
            }
        }
        Ok(())
    }
}

/// 测试专用：把 native 声明与静态桥写进目标 store。
pub fn ensure_test_isa(kv: &mut Kv) {
    let _ = kv.mkindex("/‥isa/");
    if let Ok(native) = parse_native() {
        for e in &native {
            let _ = ensure_native(kv, e);
        }
    }
    const BRIDGES: &str = include_str!("../tests/fixtures/def_rwir.txt");
    for line in BRIDGES.lines() {
        let c: Vec<&str> = line.split('\t').collect();
        if c.len() != 4 {
            continue;
        }
        let nr: u32 = c[1].parse().unwrap_or(0);
        let nw: u32 = c[2].parse().unwrap_or(0);
        ensure_test_bridge(kv, c[0], nr, nw);
    }
}

pub fn ensure_test_bridge(kv: &mut Kv, op: &str, nr: u32, nw: u32) {
    let base = keytree::isa_op(op);
    let mut pairs: Vec<(String, String, Vec<u8>)> = vec![(
        base.clone(),
        kvkind::KIND_DEF_RWIR.to_string(),
        kvkind::new_defrwir(nr as i32, nw as i32, false),
    )];
    for j in 0..nr {
        pairs.push((
            format!("{base}/[0,-{}]", j + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_langtype("*"),
        ));
    }
    for j in 0..nw {
        pairs.push((
            format!("{base}/[0,{}]", j + 1),
            kvkind::KIND_DEF_LANGTYPE.to_string(),
            kvkind::new_def_langtype("*"),
        ));
    }
    let _ = kv.set(&pairs);
}
