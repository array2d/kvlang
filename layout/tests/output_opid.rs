//! 输出算子编成十进制 opid 的集成测试。
//!
//! 验的是 layout 的产出：编译含 `println` / `print` / `cerr` 的源码后，
//! 指令槽里应当是**十进制 opid 字符串**（`65281` 等），不是算子名。
//!
//! 为什么是十进制而不是新 token：wire 形状不变（指令槽仍是字符串），
//! runtimec 的 encoder 只按「槽名全为数字即 opid」处理，一个算子名都不认识。
//! 端到端（`binrwir_dump` 显示 opid）要等 runtimec 侧的数字回退落地，
//! 本文件只验 layout 这一半——那半边归 coding4 的 TASK-051。

use kvlanglayout::code::{compile, init_dirs};
use kvlanglayout::kvkind;
use kvlanglayout::{ffi::Kv, keytree};

/// 每条测试自己的 store，Drop 时删干净（残留会累积到 /tmp 撑爆 pod）。
struct TempStore(String);
impl Drop for TempStore {
    fn drop(&mut self) {
        /* shm 后端会在 `<path>.head` / `<path>.data` 旁生成配套文件，
         * 一起删掉，别留残渣。 */
        let base = self.0.trim_start_matches("shm://");
        let _ = std::fs::remove_file(base);
        for suffix in [".head", ".data", ".gen", ".garb"] {
            let _ = std::fs::remove_file(format!("{base}{suffix}"));
        }
    }
}

fn fresh_kv() -> (TempStore, Kv) {
    /* 每个测试自己的 store：同一个 store 被多个测试并发打开会互相踩
     * （`kvspace connect failed`）。用「进程 id + 递增序号」区分——`line!()`
     * 在这里是常量（同一个函数），区分不了。 */
    static NEXT: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
    let ordinal = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let dsn = format!(
        "shm:///tmp/kvlang_output_opid_{}_{}.shm",
        std::process::id(),
        ordinal
    );
    let store = TempStore(dsn.clone());
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    (store, kv)
}

/// 取某个函数**全部指令槽**的算子名/opid，按 irseq 升序。
///
/// 不硬编码 irseq：签名槽 `[0,0]` 也在同一命名空间里，`[i,0]` 的 i 从 1 起
/// 但**哪些 i 是指令取决于前面写了几条声明**——硬编码会读到签名槽或越界。
/// 改成「扫一遍取 operator 槽」，测试才对源码结构免疫。
fn ops_of(kv: &mut Kv, func: &str) -> Vec<(i32, String)> {
    let prefix = format!("/lib/{func}/");
    let mut out = Vec::new();
    for child in kv.list(&prefix, false, false) {
        let name = child.trim_end_matches('/');
        /* 只要 `[i,0]` 形态：i>=1 是指令头，i==0 是签名。 */
        let Some(inner) = name.strip_prefix('[').and_then(|x| x.strip_suffix(",0]")) else {
            continue;
        };
        let Ok(irseq) = inner.parse::<i32>() else {
            continue;
        };
        if irseq < 1 {
            continue;
        }
        let key = format!("{prefix}{name}");
        let wire = kv.get_one(&key);
        out.push((irseq, kvkind::display(&wire, &kv.langtype_of(&key))));
    }
    out.sort_by_key(|(i, _)| *i);
    out
}

#[test]
fn output_ops_become_decimal_opid() {
    let (_store, mut kv) = fresh_kv();
    // 三个输出算子各占一条指令；rwir 声明由测试自己给（layout 不产 builtin 声明）。
    let src = "\
rwir println(*[]char/utf32 #s...)
rwir print(*[]char/utf32 #s...)
rwir cerr(*[]char/utf32 #s...)
rwfunc main() -> () {
    println(\"a\")
    print(\"b\")
    cerr(\"c\")
}
";
    compile(&mut kv, src).unwrap();

    /* `display()` 给出的是 `rwir:(nr=1,nw=0) <op>` 这种完整形态，取末段才是
     * 写进槽的算子名/opid。 */
    let ops: Vec<String> = ops_of(&mut kv, "main")
        .into_iter()
        .map(|(_, o)| o.rsplit(' ').next().unwrap_or("").to_string())
        .collect();
    /* 只比前三条（三条输出调用）；末尾的 `return` 是编译器补的收尾指令，
     * 与本次改动无关，它的 display 形态也不同（`rwir:return` 无空格分段）。 */
    assert_eq!(
        &ops[..3],
        ["65281", "65282", "65283"],
        "三条输出算子应依次编成 0xFF01/02/03 的十进制，实际 {ops:?}"
    );
}

#[test]
fn ordinary_ops_keep_their_names() {
    let (_store, mut kv) = fresh_kv();
    // 非输出算子必须原样保留名字——扩展 rwir 走声明 ABI，不经 opid 这条路。
    /* 用形参而不是常量：`a:int64 = 1 + 2` 会被常量折叠成一条赋值，
     * 那样测不到 `add` 这条算子的名字有没有被改写。 */
    let src = "rwfunc main(*int64 #a, *int64 #b) -> (*int64 #c) {\n    *a + *b -> *c\n}\n";
    compile(&mut kv, src).unwrap();
    let ops: Vec<String> = ops_of(&mut kv, "main")
        .into_iter()
        .map(|(_, o)| o.rsplit(' ').next().unwrap_or("").to_string())
        .collect();
    assert!(
        ops.contains(&"add".to_string()),
        "普通算子应保留名字，实际 {ops:?}"
    );
}

#[test]
fn package_prefixed_output_name_is_not_rewritten() {
    let (_store, mut kv) = fresh_kv();
    // `x·println` 是用户声明的另一条 rwir，不是内建输出。若被误编成 opid，
    // 等于悄悄改写了用户代码的语义——这是本测试要钉住的边界。
    let src = "\
rwir x·println(*[]char/utf32 #s...)
rwfunc main() -> () {
    x·println(\"a\")
}
";
    compile(&mut kv, src).unwrap();
    let ops: Vec<String> = ops_of(&mut kv, "main")
        .into_iter()
        .map(|(_, o)| o.rsplit(' ').next().unwrap_or("").to_string())
        .collect();
    assert!(
        ops.contains(&"x·println".to_string()),
        "带包前缀的名字不该被改写，实际 {ops:?}"
    );
}

#[test]
fn opid_table_matches_reserved_region() {
    // 保留区是 0xFF00–0xFFFF。三个 opid 必须落在区内，否则会被执行器当成
    // 普通算子编号，与真实能力行相撞（这正是十进制换算出错时的表现）。
    for name in ["println", "print", "cerr"] {
        let id = kvlanglayout::builtin::output_opid(name).unwrap();
        assert!(
            id >= 0xFF00,
            "{name} 的 opid {id:#06x} 不在保留区 0xFF00–0xFFFF 内"
        );
    }
    // keytree 只是为了让这个测试文件与其它 layout 测试保持同一组依赖。
    let _ = keytree::MEMBER_SEP;
}
