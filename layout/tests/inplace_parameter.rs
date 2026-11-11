//! `inplace` 写参修饰符的三条深度校验（spec `04-layout语义/04-函数写槽与布局`）。
//!
//! 判据是「**同一条指令**内既读又写」，不是「先后都出现过」——后者是「先读后覆盖」，
//! 必须拒绝。三条各自有正例与反例。

mod support;

use kvlanglayout::{compile, format, init_dirs, kvkind, printlib, vet, vet_with_store, Kv};
use std::sync::atomic::{AtomicUsize, Ordering};
use support::TempDirectory;

static SEQ: AtomicUsize = AtomicUsize::new(0);

fn fresh_kv() -> (TempDirectory, Kv) {
    let dir = TempDirectory::new(&format!(
        "inplace_parameter_{}",
        SEQ.fetch_add(1, Ordering::SeqCst)
    ));
    let mut kv = Kv::conn(&dir.dsn());
    init_dirs(&mut kv).unwrap();
    (dir, kv)
}

/// 编译（含 `validate_writes`），返回是否通过。
fn passes(src: &str) -> bool {
    vet(src).is_ok()
}

/// 编译失败时的诊断消息（用于断言「缺的是哪一条」）。
fn diagnostic(src: &str) -> String {
    match vet(src) {
        Ok(()) => String::new(),
        Err(message) => message,
    }
}

#[test]
fn inplace_positive_same_instruction_read_write() {
    // 正例：`*acc + d -> *acc` 同一条指令既读又写。
    let src = "\
rwfunc add_to(*int64 #n, int64 #d) -> (inplace *int64 #acc) {
\t*acc + d -> *acc
}
";
    assert!(
        passes(src),
        "同指令读写的 inplace 应当通过；诊断={:?}",
        diagnostic(src)
    );
}

#[test]
fn inplace_negative_never_read() {
    // 反例 1：只写不读 ⇒ 缺第 1 条。
    let src = "\
rwfunc only_write(*int64 #a) -> (inplace *int64 #acc) {
\t5 -> *acc
}
";
    let message = diagnostic(src);
    assert!(!message.is_empty(), "只写不读的 inplace 必须被拒");
    assert!(
        message.contains("从未被读"),
        "诊断应指名第 1 条（从未被读），实际={message:?}"
    );
}

#[test]
fn inplace_negative_never_write() {
    // 反例 2：只读不写 ⇒ 缺第 2 条。
    let src = "\
rwfunc only_read(*int64 #a) -> (inplace *int64 #acc) {
\t*acc -> x:int64
\tprint(x)
}
";
    let message = diagnostic(src);
    assert!(!message.is_empty(), "只读不写的 inplace 必须被拒");
    assert!(
        message.contains("从未被写"),
        "诊断应指名第 2 条（从未被写），实际={message:?}"
    );
}

#[test]
fn inplace_negative_read_then_overwrite() {
    /* 反例 3（架构师点名的那条）：先读后覆盖。
     * 满足 1、2，但没有**同一条指令**同时读写 ⇒ 必须拒，且诊断指第 3 条。 */
    let src = "\
rwfunc read_then_overwrite(*int64 #a) -> (inplace *int64 #acc) {
\t*acc -> x:int64
\t5 -> *acc
\tprint(x)
}
";
    let message = diagnostic(src);
    assert!(
        !message.is_empty(),
        "`*p -> x; 5 -> *p` 不是 inplace，必须被拒"
    );
    assert!(
        message.contains("没有**同一条指令**同时读写它"),
        "诊断应指名第 3 条（无同指令读写），实际={message:?}"
    );
}

#[test]
fn inplace_rejected_on_read_parameter() {
    // `inplace` 只能修饰写参；读参上出现它是错误。
    let src = "\
rwfunc bad(inplace *int64 #a) -> (*int64 #acc) {
\t*a + 1 -> *acc
}
";
    /* 这条的判据是**解析期**拒绝（`is_read_side` 在 `parse_param_list_side` 里
     * 就知道自己是读参），所以 `vet` 返回的是汇总串，详细消息在 stderr。
     * 断言「被拒」即可——消息原文由 CLI 侧验（见交付报告）。 */
    assert!(!passes(src), "读参上的 inplace 必须被拒");
}

#[test]
fn inplace_multiple_parameters_each_validated() {
    // 多个 inplace 写参逐个校验：第二个缺「同指令读写」，必须报。
    let src = "\
rwfunc two(*int64 #d) -> (inplace *int64 #a, inplace *int64 #b) {
\t*a + d -> *a
\tb -> x:int64
\t5 -> *b
}
";
    let message = diagnostic(src);
    assert!(
        !message.is_empty(),
        "第二个 inplace 参数不满足第 3 条，必须报"
    );
    assert!(
        message.contains("`b`"),
        "诊断应指向不满足的那个参数 `b`，实际={message:?}"
    );
}

#[test]
fn without_inplace_modifier_the_same_bodies_are_accepted() {
    /* 反向对照：**去掉修饰符**后，同样三个函数体都应当通过。
     *
     * 这一条防的是「校验写成了无条件规则」——如果某个实现把「同指令读写」当成
     * 对所有写参的要求，那普通的 `5 -> *acc` 也会被拒，这里就会红。 */
    for body in [
        "5 -> *acc\n",
        "*acc -> x:int64\n\tprint(x)\n",
        "*acc -> x:int64\n\t5 -> *acc\n\tprint(x)\n",
    ] {
        let src = format!("rwfunc f(*int64 #a) -> (*int64 #acc) {{\n\t{body}}}\n");
        assert!(
            passes(&src),
            "没有 inplace 修饰符时该函数体应当通过；诊断={:?}",
            diagnostic(&src)
        );
    }
}

#[test]
fn rwir_decl_supports_inplace_on_write_parameter_and_rejects_on_read() {
    let valid = "rwir accumulate(*int64 #delta) -> (inplace *int64 #acc)\n";
    assert!(
        passes(valid),
        "inplace on an rwir write param must pass; diag={:?}",
        diagnostic(valid)
    );
    let formatted = format(valid).unwrap();
    assert!(
        formatted.contains("inplace *int64 #acc"),
        "format must keep inplace on rwir write params, got={formatted:?}"
    );

    let invalid = "rwir bad_op(inplace *int64 #delta) -> (*int64 #acc)\n";
    assert!(
        !passes(invalid),
        "inplace on an rwir read param must be rejected"
    );
    assert!(
        diagnostic(invalid).contains("rwfunc 或 rwir 的写参"),
        "diagnostic must name rwfunc or rwir write params, got={:?}",
        diagnostic(invalid)
    );
}

#[test]
fn compile_persists_inplace_in_kvspace_and_printlib_reconstructs_it() {
    let (_store, mut kv) = fresh_kv();
    let src = concat!(
        "rwir native_acc(*int64 #d) -> (inplace *int64 #acc)\n",
        "rwfunc add_to(*int64 #d) -> (inplace *int64 #acc) {\n",
        "    *acc + *d -> *acc\n",
        "}\n",
    );
    compile(&mut kv, src).unwrap();

    // 1. /lib/add_to.[0,1] stores the mark; the body is the bare langtype.
    // 参数名是源码注释（`#name`），不落库——body 里既没有名字，也没有 `\0` 分隔符。
    let write_param_wire = kv.get_one("/lib/add_to.[0,1]");
    assert_eq!(
        kvkind::def_param_full(&write_param_wire),
        Some(("int64".to_string(), true))
    );
    let read_param_wire = kv.get_one("/lib/add_to.[0,-1]");
    assert_eq!(
        kvkind::def_param_full(&read_param_wire),
        Some(("int64".to_string(), false))
    );

    // 2. def rwir slot /lib/native_acc/[0,1] stores the mark
    let rwir_write_wire = kv.get_one("/lib/native_acc/[0,1]");
    assert_eq!(
        kvkind::def_langtype_full(&rwir_write_wire),
        Some(("int64".to_string(), true))
    );
    let rwir_read_wire = kv.get_one("/lib/native_acc/[0,-1]");
    assert_eq!(
        kvkind::def_langtype_full(&rwir_read_wire),
        Some(("int64".to_string(), false))
    );

    // 3. .src and printlib keep the mark; printlib(/lib) does not print a def rwir as an empty rwfunc
    let src_copy = kvkind::value_string(&kv.get_one("/lib/add_to.src"));
    assert!(
        src_copy.contains("inplace *int64 #acc"),
        ".src must keep inplace before the type, got={src_copy:?}"
    );
    let dumped = printlib(&mut kv, "/lib/add_to");
    assert!(
        dumped.contains("inplace int64)"),
        "printlib must restore inplace from .[0,1], got={dumped:?}"
    );
    let full_dump = printlib(&mut kv, "/lib");
    assert!(
        !full_dump.contains("rwfunc native_acc()"),
        "printlib(/lib) printed a def rwir as an empty rwfunc, got={full_dump:?}"
    );
    assert!(
        full_dump.contains("/lib/native_acc/[0,1] def langtype:int64 inplace"),
        "printlib(/lib) must list the def rwir write slot, got={full_dump:?}"
    );

    // 4. Lowering loads `*acc` into a temp first, so every step of add_to replays alone.
    assert_eq!(record(&mut kv, "/lib/add_to"), "ALWAYS_RESUMABLE");
}

#[test]
fn inplace_supports_member_writes_and_delegation_and_crash_resume_propagation() {
    // 1. Member read and write in one instruction: `(*p)·x + *d -> (*p)·x`
    let member_src = "\
struct Counter {
    x:int64=0
}
rwfunc bump(*int64 #d) -> (inplace */lib/Counter #p) {
    (*p)·x + *d -> (*p)·x
}
";
    assert!(
        passes(member_src),
        "member read+write in one instruction must pass; diag={:?}",
        diagnostic(member_src)
    );

    // 2. Delegating to inplace rwir/rwfunc, and the callers' crash_resume records
    let (_store, mut kv) = fresh_kv();
    let delegate_src = concat!(
        "rwir native_acc(*int64 #d) -> (inplace *int64 #acc)\n",
        "rwfunc wrap_acc(*int64 #d) -> (inplace *int64 #acc) {\n",
        "    native_acc(*d) -> *acc\n",
        "}\n",
        "rwfunc local_caller() -> () {\n",
        "    10 -> v:int64\n",
        "    wrap_acc(5) -> v\n",
        "}\n",
        "rwfunc global_caller() -> () {\n",
        "    10 -> /tmp/inplace_global_target\n",
        "    wrap_acc(5) -> /tmp/inplace_global_target\n",
        "}\n",
    );
    compile(&mut kv, delegate_src).unwrap();

    /* A user rwir is unverified: it never joins a longer range, entry or caller. */
    assert_eq!(
        record(&mut kv, "/lib/wrap_acc"),
        "NOT_RESUMABLE:1:unverified:native_acc"
    );
    assert_eq!(
        record(&mut kv, "/lib/local_caller"),
        "NOT_RESUMABLE:1:callee:wrap_acc"
    );
    assert_eq!(
        record(&mut kv, "/lib/global_caller"),
        "NOT_RESUMABLE:1:callee:wrap_acc"
    );
}

// ── crash/resume verdicts (layout/src/crash_resume.rs) ─────────────────

fn record(kv: &mut Kv, func_dir: &str) -> String {
    kvkind::value_string(&kv.get_one(&format!("{func_dir}/\u{2025}crash_resume")))
}

/// Lay `src` out into a fresh store and return the record of each function in `names`.
fn verdicts(src: &str, names: &[&str]) -> Vec<String> {
    let (_store, mut kv) = fresh_kv();
    compile(&mut kv, src).unwrap();
    names
        .iter()
        .map(|n| record(&mut kv, &format!("/lib/{n}")))
        .collect()
}

const NATIVE_ACC: &str = "rwir native_acc(*int64 #d) -> (inplace *int64 #acc)\n";

#[test]
fn crash_resume_self_increment_anchors_at_initial_write() {
    let src = "rwfunc inc() -> () {\n\tx:int64 = 1\n\tx = x + 1\n\tx -> /resume/y\n}\n";
    assert_eq!(verdicts(src, &["inc"]), ["CONDITIONALLY_RESUMABLE:1<-0"]);
}

#[test]
fn crash_resume_loop_increment_needs_entry() {
    let src = "rwfunc lp() -> () {\n\ti:int64 = 0\n\twhile (i < 10) {\n\t\ti = i + 1\n\t}\n}\n";
    assert_eq!(verdicts(src, &["lp"]), ["CONDITIONALLY_RESUMABLE:4<-entry"]);
}

/// No replay range may re-run the committed `*c + 1 -> *c`; here none is needed at all.
#[test]
fn crash_resume_window_replays_each_step_alone() {
    let src = concat!(
        "rwfunc window(*int64 #b0) -> (inplace *int64 #c, inplace *int64 #x) {\n",
        "\t*b0 + 0 -> b:int64\n\t*c + 1 -> *c\n\t*x + b -> *x\n}\n",
    );
    assert_eq!(verdicts(src, &["window"]), ["ALWAYS_RESUMABLE"]);
}

/// `caller` needs one step that both reads and writes a parameter; a user rwir blocks it.
#[test]
fn crash_resume_parameter_rmw_defers_to_caller() {
    let src = format!(
        "{NATIVE_ACC}{}{}{}{}",
        "rwfunc add_to(*int64 #d) -> (inplace *int64 #acc) {\n\t*acc + *d -> *acc\n}\n",
        "rwfunc set0() -> (inplace *[]int64 #acc) {\n\t(*acc)[0] = 9\n}\n",
        "rwfunc outer() -> (*[]int64 #x) {\n\tset0() -> *x\n}\n",
        "rwfunc wrap_acc(*int64 #d) -> (inplace *int64 #acc) {\n\tnative_acc(*d) -> *acc\n}\n",
    );
    assert_eq!(
        verdicts(&src, &["add_to", "set0", "outer", "wrap_acc"]),
        [
            "ALWAYS_RESUMABLE",
            "CONDITIONALLY_RESUMABLE:0<-caller",
            "CONDITIONALLY_RESUMABLE:0<-caller",
            "NOT_RESUMABLE:1:unverified:native_acc"
        ]
    );
}

/// The multi-step range for the global target re-reads a key the function writes.
#[test]
fn crash_resume_callers_of_a_reading_callee() {
    let src = concat!(
        "rwfunc add_to(*int64 #d) -> (inplace *int64 #acc) {\n\t*acc + *d -> *acc\n}\n",
        "rwfunc local_caller() -> () {\n\t10 -> v:int64\n\tadd_to(5) -> v\n}\n",
        "rwfunc global_caller() -> () {\n\t10 -> /tmp/g\n\tadd_to(5) -> /tmp/g\n}\n",
    );
    assert_eq!(
        verdicts(src, &["local_caller", "global_caller"]),
        ["CONDITIONALLY_RESUMABLE:1<-0", "NOT_RESUMABLE:1:/tmp/g"]
    );
}

#[test]
fn crash_resume_plain_unresolved_and_unmarked_read() {
    let src = concat!(
        "rwfunc plain(int64 #a) -> (*int64 #r) {\n\tb:int64 = a + 1\n\tb -> *r\n}\n",
        "rwfunc unres() -> () {\n\t0 -> v:int64\n\text·f(1) -> v\n}\n",
        "rwfunc bump() -> (*int64 #acc) {\n\t*acc + 1 -> *acc\n}\n",
        "rwfunc user() -> () {\n\tv:int64 = 3\n\tbump() -> v\n\tv -> /tmp/o\n}\n",
    );
    assert_eq!(
        verdicts(src, &["plain", "unres", "user"]),
        [
            "ALWAYS_RESUMABLE",
            "NOT_RESUMABLE:1:callee:ext·f",
            "CONDITIONALLY_RESUMABLE:1<-0"
        ]
    );
}

/// `a -> t; t + 1 -> a; a + t -> a` in one block: anchor after the read of live-in `a`.
#[test]
fn crash_resume_read_then_overwrite_and_join() {
    let src = concat!(
        "rwfunc rto(int64 #c) -> () {\n\ta:int64 = 1\n\tif (c > 0) {\n",
        "\t\ta -> t:int64\n\t\tt + 1 -> a\n\t\ta + t -> a\n\t\ta -> /tmp/rto\n\t}\n}\n",
        "rwfunc join(int64 #c) -> () {\n\tx:int64 = 0\n\tif (c > 0) {\n\t\tx = 5\n",
        "\t} else {\n\t\tx = 7\n\t}\n\tx = x + 1\n\tx -> /tmp/join\n}\n",
    );
    assert_eq!(
        verdicts(src, &["rto", "join"]),
        [
            "CONDITIONALLY_RESUMABLE:6<-5",
            "CONDITIONALLY_RESUMABLE:8<-entry"
        ]
    );
}

/// Reviewer repros g1, g2b, g4, g5.
#[test]
fn crash_resume_anchor_regressions() {
    let src = format!(
        "{NATIVE_ACC}{}{}{}{}",
        "rwfunc g1() -> () {\n\t1 -> s:int64\n\ts -> t:int64\n\t100 -> s\n\tnative_acc(t) -> s\n}\n",
        "rwfunc g2b() -> () {\n\t0 -> s:int64\n\t/tmp/g -> u:int64\n\tu -> v:int64\n\t7 -> /tmp/g\n\tnative_acc(v) -> s\n}\n",
        "rwfunc g4() -> () {\n\t0 -> i:int64\n\t0 -> s:int64\n\twhile (i < 3) {\n\t\tnative_acc(i) -> s\n\t\ti + 1 -> i\n\t}\n}\n",
        "rwfunc g5() -> () {\n\t0 -> i:int64\n\ta:[]int64 = [1, 2, 3]\n\twhile (i < 3) {\n\t\ta[0] = a[0] + i\n\t\ti + 1 -> i\n\t}\n}\n",
    );
    assert_eq!(
        verdicts(&src, &["g1", "g2b", "g4", "g5"]),
        [
            "NOT_RESUMABLE:3:unverified:native_acc",
            "NOT_RESUMABLE:4:unverified:native_acc",
            "NOT_RESUMABLE:5:unverified:native_acc",
            "CONDITIONALLY_RESUMABLE:7<-entry,8<-entry"
        ]
    );
}

/// Callees laid out earlier into the same store count for both the inplace check and the verdict.
#[test]
fn store_resident_callees_are_resolved() {
    let (_store, mut kv) = fresh_kv();
    compile(
        &mut kv,
        "lib mp {\n\trwir native_acc(*int64 #d) -> (inplace *int64 #acc)\n}\n",
    )
    .unwrap();
    compile(
        &mut kv,
        "lib mp {\n\trwfunc add_to(*int64 #d) -> (inplace *int64 #acc) {\n\t\t*acc + *d -> *acc\n\t}\n}\n",
    )
    .unwrap();
    let h = "lib other {\n\trwfunc h(*int64 #d) -> (inplace *int64 #acc) {\n\t\tmp·native_acc(*d) -> *acc\n\t}\n}\n";
    assert!(!passes(h), "without the store the callee mark is unknown");
    vet_with_store(&mut kv, h).unwrap();
    compile(
        &mut kv,
        concat!(
            "lib other {\n",
            "\trwfunc g() -> () {\n\t\t1 -> v:int64\n\t\tmp·native_acc(5) -> v\n\t}\n",
            "\trwfunc h(*int64 #d) -> (inplace *int64 #acc) {\n\t\tmp·native_acc(*d) -> *acc\n\t}\n",
            "\trwfunc k(*int64 #d) -> (inplace *int64 #x) {\n\t\tmp·add_to(*d) -> *x\n\t}\n",
            "}\n",
        ),
    )
    .unwrap();
    /* The store `def rwir` is found, and unverified like any declared rwir. */
    assert_eq!(
        record(&mut kv, "/lib/other·g"),
        "NOT_RESUMABLE:1:unverified:mp·native_acc"
    );
    assert_eq!(
        record(&mut kv, "/lib/other·h"),
        "NOT_RESUMABLE:1:unverified:mp·native_acc"
    );
    /* A store rwfunc body is invisible: never replayed. */
    assert_eq!(
        record(&mut kv, "/lib/other·k"),
        "NOT_RESUMABLE:1:callee:mp·add_to"
    );
}

/// A callee's mark counts only after it verifies, so recursion cannot justify itself.
#[test]
fn inplace_recursion_does_not_justify_itself() {
    for body in [
        "\tif (n > 0) {\n\t\tf(n - 1) -> *acc\n\t} else {\n\t\t5 -> *acc\n\t}\n",
        "\t5 -> *acc\n\tf(0) -> *acc\n",
    ] {
        let src = format!("rwfunc f(int64 #n) -> (inplace *int64 #acc) {{\n{body}}}\n");
        assert!(
            diagnostic(&src).contains("从未被读"),
            "recursion must not credit itself: {src}"
        );
    }
    let mutual = concat!(
        "rwfunc a(int64 #n) -> (inplace *int64 #acc) {\n\tb(n) -> *acc\n}\n",
        "rwfunc b(int64 #n) -> (inplace *int64 #acc) {\n\ta(n) -> *acc\n}\n",
    );
    assert!(!passes(mutual), "mutual recursion must not credit itself");
    let verified = concat!(
        "rwfunc add_to(*int64 #d) -> (inplace *int64 #acc) {\n\t*acc + *d -> *acc\n}\n",
        "rwfunc wrap(*int64 #d) -> (inplace *int64 #acc) {\n\tadd_to(*d) -> *acc\n}\n",
    );
    assert!(
        passes(verified),
        "a verified callee mark counts: {}",
        diagnostic(verified)
    );
}

/// Quoted literals, static member keys and shadowing loop variables are not reads of the param.
#[test]
fn inplace_reads_ignore_literals_member_keys_and_shadowing() {
    for src in [
        "rwfunc f() -> (inplace *char/utf8 #acc) {\n\t\"acc\" -> *acc\n}\n",
        "rwfunc f(int64 #d) -> (inplace *int64 #acc) {\n\tprintln(\"acc\") -> *acc\n}\n",
        "struct S {\n\tacc:int64=0\n}\nrwfunc f(*/lib/S #q) -> (inplace *int64 #acc) {\n\t(*q)·acc -> *acc\n}\n",
    ] {
        assert!(diagnostic(src).contains("从未被读"), "must be rejected as never read: {src}");
    }
    let shadow = "rwfunc f(*[]int64 #d) -> (inplace *int64 #acc) {\n\t0 -> *acc\n\tfor (acc in *d) {\n\t\tacc + 1 -> *acc\n\t}\n}\n";
    assert!(
        diagnostic(shadow).contains("shadows a parameter"),
        "{}",
        diagnostic(shadow)
    );
    /* The loop variable would be emitted as the parameter slot; read params were already refused. */
    let write_shadow =
        "rwfunc f(*[]int64 #d) -> (*int64 #r) {\n\tfor (r in *d) {\n\t\tprint(r)\n\t}\n}\n";
    assert!(
        diagnostic(write_shadow).contains("shadows a parameter"),
        "{}",
        diagnostic(write_shadow)
    );
    assert!(!passes(
        "rwfunc f(*[]int64 #d) -> () {\n\tfor (d in *d) {\n\t\tprint(d)\n\t}\n}\n"
    ));
}

/// `(*m)·*k` desugars to a `kvlang·resolvekey` position slot; the write still counts for `m`.
#[test]
fn inplace_dynamic_member_write_counts() {
    let src = "rwfunc put(*int64 #k, *int64 #v) -> (inplace *'[int64]'int64 #m) {\n\t(*m)·*k + *v -> (*m)·*k\n}\n";
    assert!(
        passes(src),
        "dynamic member RMW must pass: {}",
        diagnostic(src)
    );
}

/// `vet` with a store accepts a call to an rwir declared in the same file, as `compile` does.
#[test]
fn vet_with_store_sees_same_unit_rwir_decls() {
    let src = "rwir native_acc(*int64 #d) -> (*int64 #acc)\nrwfunc w(*int64 #d) -> (*int64 #acc) {\n\tnative_acc(*d) -> *acc\n}\n";
    let (_store, mut kv) = fresh_kv();
    assert!(
        vet_with_store(&mut kv, src).is_ok(),
        "{:?}",
        vet_with_store(&mut kv, src)
    );
    let (_store2, mut kv2) = fresh_kv();
    assert!(compile(&mut kv2, src).is_ok());
}

/// `format` output of a dynamic member RMW still verifies.
#[test]
fn inplace_dynamic_member_write_survives_format() {
    let src = "rwfunc put(*int64 #k, *int64 #v) -> (inplace *'[int64]'int64 #m) {\n\t(*m)·*k + *v -> (*m)·*k\n}\n";
    let formatted = format(src).unwrap();
    assert!(
        passes(&formatted),
        "{formatted}\n{}",
        diagnostic(&formatted)
    );
}

/// Duplicate definitions: the last one is what the store keeps, so it decides the credit.
#[test]
fn inplace_credit_uses_last_duplicate_definition() {
    let rwfunc = concat!(
        "rwfunc g(*int64 #d) -> (inplace *int64 #acc) {\n\t*acc + *d -> *acc\n}\n",
        "rwfunc g(*int64 #d) -> (*int64 #acc) {\n\t*d -> *acc\n}\n",
        "rwfunc f(*int64 #d) -> (inplace *int64 #r) {\n\tg(*d) -> *r\n}\n",
    );
    assert!(
        diagnostic(rwfunc).contains("从未被读"),
        "{}",
        diagnostic(rwfunc)
    );
    let rwir = concat!(
        "rwir n(*int64 #d) -> (inplace *int64 #acc)\n",
        "rwir n(*int64 #d) -> (*int64 #acc)\n",
        "rwfunc f(*int64 #d) -> (inplace *int64 #r) {\n\tn(*d) -> *r\n}\n",
    );
    assert!(
        diagnostic(rwir).contains("从未被读"),
        "{}",
        diagnostic(rwir)
    );
}

/// An rwir decl reusing an operator word gets no credit: the call is emitted as the builtin.
#[test]
fn inplace_operator_word_rwir_decl_gets_no_credit() {
    let src = "rwir add(*int64 #a, *int64 #b) -> (inplace *int64 #acc)\nrwfunc f(int64 #x) -> (inplace *int64 #r) {\n\tadd(x, 1) -> *r\n}\n";
    assert!(diagnostic(src).contains("从未被读"), "{}", diagnostic(src));
}
