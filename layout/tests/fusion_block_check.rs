
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
    assert!(per_instruction_ok(&block), "per-instruction check should pass [b=a; a=1]");
}

#[test]
fn block_check_rejects_the_counterexample() {
    let block = [instr(&[A], &[B], false), instr(&[], &[A], false)];
    assert!(
        !later_write_recovered(&block),
        "C1* must reject [b=a; a=1]: i₁ reads a, later i₂ writes a, and no earlier write restores a"
    );
    assert!(!no_future_read(&block), "C1' must also reject [b=a; a=1]");
    assert!(!block_admissible(&block), "block-level criterion must reject [b=a; a=1]");
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
        "C1* should accept [a=1; b=a; a=2]: earlier i₀ restores a"
    );
    assert!(!no_future_read(&block), "C1' is a conservative approximation and should reject this valid block");
    assert!(block_admissible(&block), "block-level criterion should accept valid block");
}

#[test]
fn inplace_instruction_rejects_the_block() {
    
    let block = [instr(&[A], &[A], true)];
    assert!(!per_instruction_ok(&block), "inplace instruction must be rejected by C1");
    assert!(!block_admissible(&block));
}
