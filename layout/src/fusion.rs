
#[derive(Debug, Clone, Copy)]
pub struct FusionInstr<'a> {
    pub reads: &'a [&'a str],
    pub writes: &'a [&'a str],
    pub inplace: bool,
}

pub fn per_instruction_ok(instrs: &[FusionInstr<'_>]) -> bool {
    instrs
        .iter()
        .all(|i| !i.inplace && i.reads.iter().all(|r| !i.writes.contains(r)))
}

pub fn no_future_read(instrs: &[FusionInstr<'_>]) -> bool {
    instrs.iter().enumerate().all(|(j, i)| {
        i.reads
            .iter()
            .all(|k| !instrs[j + 1..].iter().any(|later| later.writes.contains(k)))
    })
}

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

pub fn block_admissible(instrs: &[FusionInstr<'_>]) -> bool {
    per_instruction_ok(instrs) && later_write_recovered(instrs)
}
