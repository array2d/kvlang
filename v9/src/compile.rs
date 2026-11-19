//! O0 编译：layout 的明文槽 → binrwir 格序列。
//!
//! 格式见 `v9/docs/bin-format.md`。只覆盖 iops 用到的 11 条指令的形态。

pub const CELL: usize = 8;

/// 操作数：从 layout 槽的 `(body, langtype)` 推出编码。
pub enum Operand {
    /// 标量常量：占一格字面量，操作数格存格号。
    Constant(Vec<u8>),
    /// 帧局部：addrbuf 索引（建帧期填真实地址）。
    FrameLocal { name: String, deref: bool },
    /// 分支目标：`s0` 指令序号。
    Target(i64),
}

impl Operand {
    /// 从槽的 body 与 langtype 判形态。
    ///
    /// layout 的槽 body 有两种形态：
    ///   - `int64` 等标量：body 就是原始小端字节；
    ///   - `rwir`：body = `名字\0langtype`（或 `*[0,1] int64` 这种带空格的）。
    pub fn from_slot(langtype: &str, body: &[u8]) -> Option<Operand> {
        if langtype == "rwir" {
            /* body = `[5 字节头][名字]0[langtype]`；名字是 NUL 前那一段。 */
            let text = String::from_utf8_lossy(&body[5.min(body.len())..]).into_owned();
            let name = text.split('\0').next().unwrap_or("").trim().to_string();
            /* `*[0,1]` 的 `*` 是 deref 位，建帧期剥掉前缀但位保留（spec §五之二）。 */
            let (deref, name) = match name.strip_prefix('*') {
                Some(rest) => (true, rest.to_string()),
                None => (false, name),
            };
            return Some(Operand::FrameLocal { name, deref });
        }
        /* 标量：body 就是值。窄类型也占整格（高位补零）。 */
        let mut cell = [0u8; CELL];
        let n = body.len().min(CELL);
        cell[..n].copy_from_slice(&body[..n]);
        Some(Operand::Constant(cell.to_vec()))
    }
}

/// op 字：`opcode16 | read4 | write4 | next_distance8 | reserved32`。
pub fn op_word(opcode: u16, reads: u8, writes: u8, next_distance: u8) -> [u8; CELL] {
    let word = (opcode as u64)
        | ((reads as u64) << 16)
        | ((writes as u64) << 20)
        | ((next_distance as u64) << 24);
    word.to_le_bytes()
}

/// 操作数格：`kind 2 位 + payload 62 位`，deref 位在 bit 63。
pub fn operand_cell(kind: u8, payload: u64, deref: bool) -> [u8; CELL] {
    let mut word = ((kind as u64) << 62) | (payload & ((1u64 << 62) - 1));
    if deref {
        word |= 1u64 << 63;
    }
    word.to_le_bytes()
}

pub const KIND_LITERAL: u8 = 0;
pub const KIND_ADDRBUF: u8 = 2;
pub const KIND_TARGET: u8 = 3;


/// 定宽类型的字节宽度；变宽类型（`[]char/*`、容器）返回 None，
/// 调用方按 spec §十二 报错而不是猜一个宽度。
pub fn scalar_width(langtype: &str) -> Option<u64> {
    let w = match langtype {
        "bool" | "int8" | "uint8" => 1,
        "int16" | "uint16" => 2,
        "int32" | "uint32" | "float32" | "char/utf32" => 4,
        "int64" | "uint64" | "float64" => 8,
        _ => return None,
    };
    Some(w)
}
