//! `inplace` 写参修饰符的三条深度校验（spec `04-layout语义/04-函数写槽与布局`）。
//!
//! 判据是「**同一条指令**内既读又写」，不是「先后都出现过」——后者是「先读后覆盖」，
//! 必须拒绝。三条各自有正例与反例。

use kvlanglayout::vet;

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
rwfunc add_to(*int64 $n, int64 $d) -> (*int64 $acc inplace) {
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
rwfunc only_write(*int64 $a) -> (*int64 $acc inplace) {
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
rwfunc only_read(*int64 $a) -> (*int64 $acc inplace) {
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
rwfunc read_then_overwrite(*int64 $a) -> (*int64 $acc inplace) {
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
rwfunc bad(*int64 $a inplace) -> (*int64 $acc) {
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
rwfunc two(*int64 $d) -> (*int64 $a inplace, *int64 $b inplace) {
\t*a + d -> *a
\tb -> x:int64
\t5 -> *b
}
";
    let message = diagnostic(src);
    assert!(!message.is_empty(), "第二个 inplace 参数不满足第 3 条，必须报");
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
        let src = format!("rwfunc f(*int64 $a) -> (*int64 $acc) {{\n\t{body}}}\n");
        assert!(
            passes(&src),
            "没有 inplace 修饰符时该函数体应当通过；诊断={:?}",
            diagnostic(&src)
        );
    }
}
