use std::collections::HashMap;

use super::ast::{self, Instruction, Stmt};

fn is_terminator(s: &Instruction) -> bool {
    match &s.expr {
        Some(e) if e.is_leaf() => e.val == "return",
        Some(e) => matches!(e.op.as_str(), "return" | "goto" | "br"),
        None => false,
    }
}

fn ret_inst() -> Instruction {
    Instruction {
        comments: Vec::new(),
        expr: Some(ast::leaf("return")),
        writes: Vec::new(),
        write_types: Vec::new(),
        arrow_left: false,
        eq: false,
    }
}

fn is_irseq_coord(s: &str) -> bool {
    let b = s.as_bytes();
    if b.len() < 5 || b[0] != b'[' || !s.ends_with(",0]") {
        return false;
    }
    b[1..s.len() - 3].iter().all(|c| c.is_ascii_digit())
}

pub fn flatten_linear(body: &[Stmt]) -> (Vec<Instruction>, HashMap<String, i32>) {
    let mut insts = Vec::new();
    let mut labels = HashMap::new();
    walk(body, &mut insts, &mut labels);
    if insts.is_empty() || !is_terminator(insts.last().unwrap()) {
        insts.push(ret_inst());
    }
    (insts, labels)
}

fn walk(stmts: &[Stmt], insts: &mut Vec<Instruction>, labels: &mut HashMap<String, i32>) {
    for st in stmts {
        match st {
            Stmt::Instruction(s) => insts.push(s.clone()),
            Stmt::Scope(s) => {
                if labels.contains_key(&s.label) {
                    panic!("flatten_linear: duplicate label {}", s.label);
                }
                let start = insts.len() as i32 + 1;
                labels.insert(s.label.clone(), start);
                walk(&s.body, insts, labels);
                let contributed = insts.len() as i32 + 1 - start;
                if contributed <= 0 || !is_terminator(insts.last().unwrap()) {
                    insts.push(ret_inst());
                }
            }
            other => panic!(
                "flatten_linear: {} survived lower — expected Instruction|Scope",
                other.first_line()
            ),
        }
    }
}

pub fn rewrite_jumps(insts: &mut [Instruction], labels: &HashMap<String, i32>) {
    for inst in insts {
        let e = inst
            .expr
            .as_mut()
            .unwrap_or_else(|| panic!("rewrite_jumps: Instruction has no expr"));
        match e.op.as_str() {
            "goto" => {
                if e.args.len() != 1 || !e.args[0].is_leaf() {
                    panic!("rewrite_jumps: goto must have one leaf arg");
                }
                rewrite_label(&mut e.args[0].val, labels);
            }
            "br" => {
                if e.args.len() != 3 || !e.args[1].is_leaf() || !e.args[2].is_leaf() {
                    panic!("rewrite_jumps: br must have 3 args with leaf labels");
                }
                rewrite_label(&mut e.args[1].val, labels);
                rewrite_label(&mut e.args[2].val, labels);
            }
            _ => {}
        }
    }
}

fn rewrite_label(name: &mut String, labels: &HashMap<String, i32>) {
    if is_irseq_coord(name) {
        panic!("rewrite_jumps: flatten runs once, already a coord {name}");
    }
    let n = *labels
        .get(name)
        .unwrap_or_else(|| panic!("rewrite_jumps: unknown label {name}"));
    *name = format!("[{n},0]");
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lower;
    use crate::parser;

    fn flatten_src(src: &str) -> (Vec<Instruction>, HashMap<String, i32>) {
        let (file, diags) = parser::parse_code(src).unwrap();
        for d in &diags {
            eprintln!("{}", d.string());
        }
        assert!(!parser::has_errors(&diags), "parse errors");
        let fn_ = file.funcs.iter().find(|f| f.sig.name != "init").unwrap_or(&file.funcs[0]);
        let lowered = lower::lower_func(fn_);
        let (mut insts, labels) = flatten_linear(&lowered.body);
        rewrite_jumps(&mut insts, &labels);
        (insts, labels)
    }

    fn op(inst: &Instruction) -> String {
        inst.flat().0
    }

    fn reads(inst: &Instruction) -> Vec<String> {
        inst.flat().1
    }

    fn assert_jumps_in_range(insts: &[Instruction]) {
        let n = insts.len() as i32;
        assert!(n >= 1, "empty");
        for (i, inst) in insts.iter().enumerate() {
            let o = op(inst);
            if o == "goto" {
                let t = &reads(inst)[0];
                assert!(is_irseq_coord(t), "goto {t}");
                let d: i32 = t[1..t.len() - 3].parse().unwrap();
                assert!((1..=n).contains(&d), "goto {t} out of 1..={n} at {}", i + 1);
            }
            if o == "br" {
                let r = reads(inst);
                for t in [&r[1], &r[2]] {
                    assert!(is_irseq_coord(t), "br {t}");
                    let d: i32 = t[1..t.len() - 3].parse().unwrap();
                    assert!((1..=n).contains(&d), "br {t} out of 1..={n} at {}", i + 1);
                }
            }
        }
        assert!(insts.iter().any(|i| op(i) == "return"), "no return in flattened body");
    }

    #[test]
    fn simple_add_gets_trailing_return() {
        let (insts, labels) = flatten_src("rwfunc sum(A:int64, B:int64) -> (C:int64) {\n    A + B -> C\n}\n");
        assert!(labels.is_empty());
        assert_eq!(insts.len(), 2);
        assert_eq!(op(&insts[0]), "+");
        assert_eq!(op(&insts[1]), "return");
    }

    #[test]
    fn nested_if_does_not_bind_empty_merge_to_inner() {
        let src = "rwfunc f(A:int64, B:int64) -> (X:int64) {\n    if (A > 0) {\n        if (B > 0) {\n            1 -> X\n        }\n    }\n}\n";
        let (insts, labels) = flatten_src(src);
        assert_jumps_in_range(&insts);
        // empty outer merge must occupy its own return, not the inner if's first inst
        let if_starts: std::collections::HashSet<i32> = labels
            .iter()
            .filter(|(k, _)| k.contains("_if_"))
            .map(|(_, n)| *n)
            .collect();
        assert!(!if_starts.is_empty(), "labels {labels:?}");
        for (k, n) in &labels {
            if k.contains("merge") {
                assert!(
                    !if_starts.contains(n),
                    "merge {k} bound to if start {n} — empty merge swallowed the inner block"
                );
            }
        }
    }

    #[test]
    fn while_false_does_not_fall_into_inner_if() {
        let src = "rwfunc g(N:int64, hit:bool) -> (Y:int64) {\n    N -> C\n    0 -> Y\n    while (C > 0) {\n        if (hit) {\n            1 -> Y\n        }\n        C - 1 -> C\n    }\n}\n";
        let (insts, labels) = flatten_src(src);
        assert_jumps_in_range(&insts);
        let exit_keys: Vec<_> = labels.keys().filter(|k| k.contains("exit")).cloned().collect();
        assert!(!exit_keys.is_empty(), "labels {labels:?}");
        for k in &exit_keys {
            let n = labels[k];
            let o = op(&insts[(n - 1) as usize]);
            assert!(o == "return" || o == "goto", "exit {k} -> {o}");
        }
    }

    #[test]
    fn if_then_more_code_merge_is_continuation() {
        let src = "rwfunc f(A:int64) -> (X:int64) {\n    if (A > 0) {\n        1 -> X\n    }\n    2 -> X\n}\n";
        let (insts, labels) = flatten_src(src);
        assert_jumps_in_range(&insts);
        let merge = labels.keys().find(|k| k.contains("merge")).unwrap();
        let n = labels[merge];
        assert_eq!(op(&insts[(n - 1) as usize]), "="); // 2 -> X  is copy
    }
}
