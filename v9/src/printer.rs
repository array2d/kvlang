//! 只读的 `.bin` 打印：把编译产物还原成人能核对的形态。
//!
//! 与 `compile.rs` 方向相反：那边写格，这边读格。两边的格解码必须同源，
//! 否则「打印出来对」与「执行器读到对」会分叉——所以这里复用同一套常量与
//! `describe_*` 语义，只多一层**结构校验**（编译期不会产出畸形，读回来可能）。

use crate::compile::{CELL, KIND_ADDRBUF, KIND_LITERAL, KIND_TARGET};

/// 解析后的产物结构。
pub struct Bin {
    pub cells: Vec<[u8; CELL]>,
    /// cell 1 声明的解析表项数。
    pub table_len: usize,
    /// 入口格（cell 0 的 next_distance）。
    pub entry: usize,
}

/// 校验失败一律 `Err`（调用方非零退出），不静默降级。
pub fn parse(bytes: &[u8], langtype: &str, bodylen: u64) -> Result<Bin, String> {
    /* langtype 必须是 `[8N]uint8`，且 N 与 bodylen 自洽。 */
    let inner = langtype
        .strip_prefix('[')
        .and_then(|s| s.strip_suffix("]uint8"))
        .ok_or_else(|| format!("langtype 不是 `[8N]uint8`：{langtype:?}"))?;
    let declared: usize = inner
        .parse()
        .map_err(|_| format!("langtype 里的 N 不是整数：{inner:?}"))?;
    if declared != bytes.len() {
        return Err(format!("langtype 的 N={declared} 与 body {} 不等", bytes.len()));
    }
    if bodylen != bytes.len() as u64 {
        return Err(format!("bodylen={bodylen} 与 body 实际 {} 不等", bytes.len()));
    }
    if bytes.len() % CELL != 0 {
        return Err(format!("body {} 字节不是 8 的倍数", bytes.len()));
    }

    let cells: Vec<[u8; CELL]> = bytes
        .chunks_exact(CELL)
        .map(|c| <[u8; CELL]>::try_from(c).expect("chunks_exact 保证 8 字节"))
        .collect();
    if cells.len() < 3 {
        return Err(format!("只有 {} 格，连 cell0 + 表头 + 一项目录都不够", cells.len()));
    }

    /* cell 0 必须是 goto，其 next 是入口格。 */
    let word0 = u64::from_le_bytes(cells[0]);
    let opcode0 = word0 & 0xFFFF;
    if opcode0 == 0 {
        return Err("cell0 的 opcode 为 0".into());
    }
    let entry = ((word0 >> 24) & 0xFF) as usize;
    if entry == 0 || entry >= cells.len() {
        return Err(format!("cell0 的入口格 {entry} 越界（共 {} 格）", cells.len()));
    }

    let table_len = u64::from_le_bytes(cells[1]) as usize;
    if 2 + table_len > cells.len() {
        return Err(format!(
            "解析表 {table_len} 项越界（2+{table_len} > {} 格）",
            cells.len()
        ));
    }
    if table_len > entry {
        return Err(format!("解析表 {table_len} 项超过入口格 {entry}"));
    }
    Ok(Bin { cells, table_len, entry })
}

/// 一格按操作数解：`kind 2 位 + payload 62 位`，deref 在 bit 63。
pub fn operand_text(word: u64) -> Result<String, String> {
    let deref = word >> 63 & 1 == 1;
    let kind = ((word >> 62) & 3) as u8;
    let payload = word & ((1u64 << 62) - 1);
    let star = if deref { "*" } else { "" };
    let body = match kind {
        KIND_LITERAL => format!("literal#{payload}"),
        KIND_ADDRBUF => format!("addrbuf#{payload}"),
        KIND_TARGET => format!("target s0={payload}"),
        other => return Err(format!("未知 kind {other}（payload={payload}）")),
    };
    Ok(format!("{star}{body}"))
}

/// op 字按位段解，并校验 `next_distance` 非零。
pub fn op_fields(word: u64) -> (u16, usize, usize, usize) {
    let opcode = (word & 0xFFFF) as u16;
    let reads = ((word >> 16) & 0xF) as usize;
    let writes = ((word >> 20) & 0xF) as usize;
    let next = ((word >> 24) & 0xFF) as usize;
    (opcode, reads, writes, next)
}
