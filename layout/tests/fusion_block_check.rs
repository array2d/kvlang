
use pathlanglayout::fusion::{
    block_admissible, later_write_recovered, no_future_read, per_instruction_ok, FusionInstr,
};

const A: &str = "a";
const B: &str = "b";

fn instr<'a>(reads: &'a [&'a str], writes: &'a [&'a str], inplace: bool) -> FusionInstr<'a> {
    FusionInstr {
        reads,
        writes,
        inplace,
    }
}

#[test]
fn per_instruction_check_passes_the_counterexample() {
    
    let block = [instr(&[A], &[B], false), instr(&[], &[A], false)];
    assert!(per_instruction_ok(&block), "逐指令检查应当放过 [b=a; a=1]");
}

#[test]
fn block_check_rejects_the_counterexample() {
    let block = [instr(&[A], &[B], false), instr(&[], &[A], false)];
    assert!(
        !later_write_recovered(&block),
        "C1* 必须拒绝 [b=a; a=1]：i₁ 读 a，更晚 i₂ 写 a，且没有更早的写恢复 a"
    );
    assert!(!no_future_read(&block), "C1' 也必须拒绝 [b=a; a=1]");
    assert!(!block_admissible(&block), "块级判据必须拒绝 [b=a; a=1]");
}

#[test]
fn block_check_accepts_a_positive_control() {
    
    let block = [
        instr(&[], &[A], false),
        instr(&[A], &[B], false),
        instr(&[], &[A], false),
    ];
    assert!(
        later_write_recovered(&block),
        "C1* 应接受 [a=1; b=a; a=2]：更早的 i₀ 会恢复 a"
    );
    assert!(!no_future_read(&block), "C1' 是保守近似，应拒绝同一个正例");
    assert!(block_admissible(&block), "块级判据应接受正例");
}

#[test]
fn inplace_instruction_rejects_the_block() {
    
    let block = [instr(&[A], &[A], true)];
    assert!(!per_instruction_ok(&block), "inplace 指令必须被 C1 拒绝");
    assert!(!block_admissible(&block));
}
