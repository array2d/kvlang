//! 融合块（Δ>1）的可执行判据参考实现。
//!
//! 对应 Lean 模型 `kvlanglean/Proof/Fusion.lean`：
//! - C1  = [`per_instruction_ok`]：每条指令非 inplace（读集与自己的写集不相交）；
//! - C1* = [`later_write_recovered`]：后向读必须被块内更早的写恢复；
//! - C1' = [`no_future_read`]：保守近似，禁止读任何「更晚指令会写」的 key；
//! - [`block_admissible`] = C1 ∧ C1*。
//!
//! 这是判据的**唯一参考实现**：任何融合实现都必须调用它，不要自己重写一套。
//! C2（外部读稳定）/ C3（提交顺序）依赖 layout/runtime 的外部信息，不在本模块内。

/// 一条指令在判据里需要的最小信息：读 key、写 key、是否 inplace。
#[derive(Debug, Clone, Copy)]
pub struct FusionInstr<'a> {
    pub reads: &'a [&'a str],
    pub writes: &'a [&'a str],
    pub inplace: bool,
}

/// C1：每条指令非 inplace，且读集与自己的写集不相交。
pub fn per_instruction_ok(instrs: &[FusionInstr<'_>]) -> bool {
    instrs
        .iter()
        .all(|i| !i.inplace && i.reads.iter().all(|r| !i.writes.contains(r)))
}

/// C1'：保守近似——一条指令不读「块内更晚指令会写」的 key。
pub fn no_future_read(instrs: &[FusionInstr<'_>]) -> bool {
    instrs.iter().enumerate().all(|(j, i)| {
        i.reads
            .iter()
            .all(|k| !instrs[j + 1..].iter().any(|later| later.writes.contains(k)))
    })
}

/// C1*：精确充分条件——若第 j 条读到的 key 会被块内更晚指令写，
/// 则块内必须有一条更早的指令也写它（重放时先恢复原值）。
pub fn later_write_recovered(instrs: &[FusionInstr<'_>]) -> bool {
    instrs.iter().enumerate().all(|(j, i)| {
        i.reads.iter().all(|k| {
            let later = instrs[j + 1..].iter().any(|x| x.writes.contains(k));
            if !later {
                return true;
            }
            instrs[..j].iter().any(|x| x.writes.contains(k))
        })
    })
}

/// 块级放行判据：C1 ∧ C1*。
///
/// 不满足时必须退回 Δ=1（逐指令提交），不允许「尽力而为」。
pub fn block_admissible(instrs: &[FusionInstr<'_>]) -> bool {
    per_instruction_ok(instrs) && later_write_recovered(instrs)
}
