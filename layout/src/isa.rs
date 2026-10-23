//! ISA registry（P1a）：控制面独立 kvspace。读 runtime-c 发布的
//! `/‥isa/‥core`/`‥meta`，校验 hash，并在 `/‥isa/func<func-path>` 写 isa-ref。
//! 数据 store 不写任何 ISA key。
//!
//! 按裁决 C：manifest/meta/isa-ref 均以现有 `[n]char/utf8` SLACK 存储；
//! 内容（多行文本）才是语义来源，langtype 只是传输标签。

use crate::ffi::{self, Kv};
use sha2::{Digest, Sha256};
use std::collections::HashMap;

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
    /// 具体槽类型（非 any、非变参尾）。
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
    pub ver: u32,
    pub count: u32,
    pub hash: String,
    pub entries: HashMap<String, Entry>,
}

fn text_of(kv: &mut Kv, key: &str) -> Option<String> {
    let v = kv.isa_get_one(key);
    if v.is_empty() || ffi::xh_is_none(&v) {
        return None;
    }
    Some(String::from_utf8_lossy(ffi::xh_body_slice(&v)).into_owned())
}

fn hex16(data: &[u8]) -> String {
    let d = Sha256::digest(data);
    d[..16].iter().map(|b| format!("{b:02x}")).collect()
}

impl Isa {
    pub fn load(kv: &mut Kv) -> Result<Isa, String> {
        let meta = text_of(kv, "/‥isa/‥meta")
            .ok_or_else(|| "ISA registry missing: no /‥isa/‥meta".to_string())?;
        let core = text_of(kv, "/‥isa/‥core")
            .ok_or_else(|| "ISA manifest missing: runtime-c".to_string())?;
        let mut lines = core.lines();
        let ver = parse_kv(lines.next(), "ver")?;
        let count: u32 = parse_kv(lines.next(), "count")?;
        let hash = parse_field(lines.next(), "hash")?;
        let records: Vec<&str> = lines.collect();
        if records.len() as u32 != count {
            return Err(format!(
                "ISA manifest count mismatch: header {count}, records {}",
                records.len()
            ));
        }
        let mut region = String::new();
        for r in &records {
            region.push_str(r);
            region.push('\n');
        }
        let got = hex16(region.as_bytes());
        if got != hash {
            return Err("ISA manifest hash mismatch".to_string());
        }
        let meta_hash = parse_field(meta.lines().find(|l| l.starts_with("hash\t")), "hash")?;
        if meta_hash != hash {
            return Err("ISA manifest hash mismatch".to_string());
        }
        let mut entries = HashMap::new();
        let mut prev: Option<&str> = None;
        for r in &records {
            let cols: Vec<&str> = r.split('\t').collect();
            if cols.len() < 4 {
                return Err("ISA manifest field count".to_string());
            }
            let nr: u32 = cols[1].parse().map_err(|_| "ISA manifest nr".to_string())?;
            let nw: u32 = cols[2].parse().map_err(|_| "ISA manifest nw".to_string())?;
            if cols.len() != 11 + nr as usize + nw as usize {
                return Err("ISA manifest field count".to_string());
            }
            if let Some(p) = prev {
                if p.as_bytes() >= cols[0].as_bytes() {
                    return Err("ISA manifest not sorted/duplicate".to_string());
                }
            }
            prev = Some(cols[0]);
            let base = 4 + nr as usize + nw as usize;
            entries.insert(
                cols[0].to_string(),
                Entry {
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
                    storetype: cols[base].parse().unwrap_or(-1),
                    headlen: cols[base + 1].parse().unwrap_or(-1),
                    width: cols[base + 2].parse().unwrap_or(-1),
                    class: cols[base + 5].to_string(),
                    line: (*r).to_string(),
                },
            );
        }
        Ok(Isa {
            ver,
            count,
            hash,
            entries,
        })
    }

    /// 写 `/‥isa/func<func-path>`（isa-ref，控制 store）。内容：ver/hash/count。
    pub fn write_ref(&self, kv: &mut Kv, func_dir: &str, ext_bridge: &str) -> Result<(), String> {
        let body = format!(
            "ver\t{}\nhash\t{}\ncount\t{}\next_bridge\t{}\n",
            self.ver, self.hash, self.count, ext_bridge
        );
        kv.isa_set(&[(ref_key(func_dir), ffi::new_char_byte(body.as_bytes()))])
            .map_err(|e| format!("ISA ref write {func_dir}: {e}"))
    }
}

fn parse_kv(line: Option<&str>, want: &str) -> Result<u32, String> {
    let l = line.ok_or_else(|| format!("ISA manifest missing {want}"))?;
    let mut it = l.split('\t');
    if it.next() != Some(want) {
        return Err(format!("ISA manifest missing {want}"));
    }
    it.next()
        .and_then(|v| v.parse().ok())
        .ok_or_else(|| format!("ISA manifest bad {want}"))
}

fn parse_field(line: Option<&str>, want: &str) -> Result<String, String> {
    let l = line.ok_or_else(|| format!("ISA manifest missing {want}"))?;
    let mut it = l.split('\t');
    if it.next() != Some(want) {
        return Err(format!("ISA manifest missing {want}"));
    }
    it.next()
        .map(str::to_string)
        .ok_or_else(|| format!("ISA manifest bad {want}"))
}

/// per-func isa-ref 的控制 store key：`/‥isa/func` + 规范化函数目录。
/// 成员分隔符 `·`(U+00B7) 在 durable 里是 memitem 分隔符（需容器声明），
/// 控制键里统一折成 `/`；runtime 侧用同一规则。
pub fn ref_key(func_dir: &str) -> String {
    let d = func_dir.trim_end_matches('/');
    assert!(d.starts_with('/'), "func dir must be absolute: {func_dir}");
    format!("/‥isa/func{}", d.replace('·', "/"))
}

/// 测试专用：把 runtime-c 发布的真实 300 条 manifest 快照写进控制 store。
/// 快照文件 `tests/fixtures/isa_core.txt` 由 runtime 的 `/‥isa/‥core` body 原样导出；
/// `isa_gate` 会比对运行时发布 hash 与快照 hash，防止两侧漂移。
pub fn ensure_test_isa(kv: &mut Kv) {
    const CORE: &str = include_str!("../tests/fixtures/isa_core.txt");
    let mut it = CORE.lines();
    let ver = it
        .next()
        .and_then(|l| l.strip_prefix("ver\t"))
        .unwrap_or("1");
    let count = it
        .next()
        .and_then(|l| l.strip_prefix("count\t"))
        .unwrap_or("300");
    let hash = it
        .next()
        .and_then(|l| l.strip_prefix("hash\t"))
        .unwrap_or("");
    let meta = format!("ver\t{ver}\ncount\t{count}\nhash\t{hash}\nowners\truntime-c\n");
    let _ = kv.isa_mkindex("/‥isa/");
    let _ = kv.isa_set(&[
        (
            "/‥isa/‥core".to_string(),
            ffi::new_char_byte(CORE.as_bytes()),
        ),
        (
            "/‥isa/‥meta".to_string(),
            ffi::new_char_byte(meta.as_bytes()),
        ),
    ]);
    // 模拟 runtime-rs 的 def rwir 注册（P1 静态桥），与 MYRWIRCAPS 同步的快照。
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

/// 测试专用：注册一个 def rwir 静态桥（模拟 runtime-rs register）。
pub fn ensure_test_bridge(kv: &mut Kv, op: &str, nr: u32, nw: u32) {
    let base = crate::keytree::rwir(op);
    let mut pairs = vec![(
        base.clone(),
        crate::kvkind::new_defrwir(nr as i32, nw as i32, false),
    )];
    for j in 0..nr {
        pairs.push((
            format!("{base}/[0,-{}]", j + 1),
            crate::kvkind::new_def_langtype("*"),
        ));
    }
    for j in 0..nw {
        pairs.push((
            format!("{base}/[0,{}]", j + 1),
            crate::kvkind::new_def_langtype("*"),
        ));
    }
    let _ = kv.set(&pairs);
}

impl Isa {
    /// P1 静态签名桥：本次发射、不在 core manifest、且 `/lib/<op>` 是 def rwir 的
    /// opcode，按 op 排序拼接 `op\trp..\twp..\n` 取 SHA-256/128；无则 `none`。
    pub fn ext_bridge(&self, kv: &mut Kv, ops: &std::collections::BTreeSet<String>) -> String {
        let mut lines: Vec<String> = Vec::new();
        for op in ops {
            if self.entries.contains_key(op) {
                continue;
            }
            let decl = kv.get_one(&crate::keytree::rwir(op));
            if crate::kvkind::kind(&decl) != crate::kvkind::KIND_DEF_RWIR {
                continue;
            }
            let b = ffi::xh_body_slice(&decl);
            if b.len() < 4 {
                continue;
            }
            let nr = u16::from_le_bytes([b[0], b[1]]) as usize;
            let nw = u16::from_le_bytes([b[2], b[3]]) as usize;
            let mut line = op.clone();
            for j in 0..nr + nw {
                let x = if j < nr {
                    -(j as i64 + 1)
                } else {
                    (j - nr) as i64 + 1
                };
                let p = kv.get_one(&format!("{}/[0,{}]", crate::keytree::rwir(op), x));
                let pb = ffi::xh_body_slice(&p);
                let ty = String::from_utf8_lossy(pb).into_owned();
                let ty = ty.strip_prefix('*').unwrap_or(&ty);
                line.push('\t');
                line.push_str(if ty.is_empty() { "any" } else { ty });
            }
            lines.push(line);
        }
        if lines.is_empty() {
            return "none".to_string();
        }
        lines.sort();
        let mut region = String::new();
        for l in &lines {
            region.push_str(l);
            region.push('\n');
        }
        let d = Sha256::digest(region.as_bytes());
        d[..16].iter().map(|b| format!("{b:02x}")).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::ref_key;

    #[test]
    fn ref_key_folds_member_sep() {
        assert_eq!(ref_key("/lib/pkg·f"), "/‥isa/func/lib/pkg/f");
        assert_eq!(ref_key("/lib/pkg·f/"), "/‥isa/func/lib/pkg/f");
        assert_eq!(ref_key("/lib/a/b·c"), "/‥isa/func/lib/a/b/c");
    }

    /// 合法 func_dir 恒为 `/lib/<pkg>·<name>`：pkg 可含 '/'（嵌套包），
    /// name 是标识符（不含 '/' 与 '·'）。折叠 `·`→`/` 后由「最后一个 '·' 位置」
    /// 可逆：最后的 pkg 段与 name 之间必有分隔，故不同函数不撞键。
    #[test]
    fn ref_key_is_injective_for_legal_func_dirs() {
        assert_ne!(ref_key("/lib/a·b"), ref_key("/lib/a/b·c"));
        assert_ne!(ref_key("/lib/a·b"), ref_key("/lib/ab"));
        assert_ne!(ref_key("/lib/a·b"), ref_key("/lib/a·b·c"));
        assert_ne!(ref_key("/lib/a/b·c"), ref_key("/lib/a/b/c·d"));
    }
}
