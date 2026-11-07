// 组件：layout/crash_resume
// 作用：对每个 rwfunc 的 rwir 数据流做静态分析，判定「在 inplace 处崩溃时能否恢复」。
// 联系：只在 layout 侧，独立可测；不改 runtimec 执行器。
// 约束：**三态，不得糊成布尔**；上溯终止条件显式；宁可多拒，不许假绿。

use std::collections::{HashMap, HashSet};

/// **允许**读写别名的算子（catalog 的 `alias policy = DECLARED`）。
///
/// 注意语义：它说的是「这个算子**允许**写参与读参别名」，**不是**「每条这样的指令
/// 都在原地改」。
///
/// `b = a` 与 `a = a` 是同一个算子，但只有后者是 inplace。
/// 所以本表只用来判「**可能**是 inplace」，真正的判据见 [`is_inplace_node`]。
pub const ALIAS_PERMITTING_OPERATORS: &[&str] = &[
    "copy",
    "compactarray·fill",
    "compactarray·set",
    "scatterarray·set",
    "scatterarray·append",
    "string·set",
    "xv·set",
    "map·new",
    "struct·new",
];

pub fn alias_permitting(operator: &str) -> bool {
    ALIAS_PERMITTING_OPERATORS.contains(&operator)
}

/// 这条指令**是不是真的 inplace**：读槽与写槽有交集。
///
/// 判据是「**同一个槽**」，不是「catalog 标了 ALIAS_DECLARED」：
///
///   - `b = a`（`a != b`）：读一处写另一处 ⇒ **不是** inplace，按普通指令处理；
///   - `a = a`（同一槽）：**是** inplace；
///   - 算子不在别名名单里 ⇒ 一定不是；
///   - **判不出**（读/写集为空、名字不可比）⇒ **保守按 inplace**，不许假绿。
pub fn is_inplace_node(operator: &str, reads: &[String], writes: &[String]) -> bool {
    if !alias_permitting(operator) {
        return false;
    }
    if reads.is_empty() || writes.is_empty() {
        /* 判不出 ⇒ 保守。空读写集的真实含义是「这条算子没有可别名的槽」，
         * 但既然判不出，就按 inplace 处理——多拒不多放。 */
        return true;
    }
    writes.iter().any(|w| reads.iter().any(|r| r == w))
}

/// 一条 rwir 的读写集快照（只保留名字，不保留类型——本分析只看数据流）。
#[derive(Debug, Clone)]
pub struct RwirNode {
    /// 该指令的稠密下标（在函数内）。
    pub identifier: usize,
    pub operator: String,
    /// 读参的槽名（已归一化）。
    pub reads: Vec<String>,
    /// 写参的槽名。
    pub writes: Vec<String>,
    pub inplace: bool,
}

/// 恢复判定三态。**不得用布尔糊成两态**：
/// 「无 inplace」与「inplace 但可上溯」对驱动的要求不同。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Resumability {
    /// 全函数无 inplace 指令：崩溃可从当前 pc 重放，与 stop/resume 等价。
    AlwaysResumable,
    /// 含 inplace，但每一处都能上溯到一条非 inplace 的 rwir 作重放起点。
    ConditionallyResumable {
        /// 每个 inplace 指令下标 → 其重放起点下标。
        replay_from: Vec<(usize, usize)>,
    },
    /// 上溯到达「已修改的全局路径」：该 vthread 不支持 crash/resume。
    NotResumable {
        /// 触雷的指令下标。
        instruction: usize,
        /// 上溯路径（从该指令一路走到全局写）。
        chain: Vec<usize>,
        /// 撞上的全局 key。
        global_key: String,
    },
}

impl Resumability {
    pub fn name(&self) -> &'static str {
        match self {
            Resumability::AlwaysResumable => "ALWAYS_RESUMABLE",
            Resumability::ConditionallyResumable { .. } => "CONDITIONALLY_RESUMABLE",
            Resumability::NotResumable { .. } => "NOT_RESUMABLE",
        }
    }
}

/// 一个函数的分析输入。
pub struct FunctionFlow {
    pub name: String,
    pub nodes: Vec<RwirNode>,
    /// 本函数写过的**绝对 key**（写集里的 `/` 开头槽）。静态保守：出现过即视为「可能已被修改」。
    pub written_globals: HashSet<String>,
}

/// 上溯终止条件（**必须显式**，见 spec）。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TraceStop {
    /// 到达一条非 inplace 指令：这是重放起点。
    NonInplace(usize),
    /// 到达函数第 0 条（vthread 起点）仍未找到：以入口为起点。
    FunctionEntry,
    /// 撞上已修改的全局路径：不可恢复。
    WrittenGlobal { instruction: usize, key: String },
    /// 上溯成环且环内全是 inplace：没有非 inplace 起点。
    CycleWithoutAnchor { cycle: Vec<usize> },
}

/// 从一条 inplace 指令出发，沿其读写参上溯到重放起点。
///
/// **自环不算「已修改的全局路径」**：`a = a + 1` 里 `a` 既是读又是写，
/// 但 `a` 是局部槽，不是全局 key。自环要记录循环边界，但结论仍是「可上溯」。
pub fn trace_back(flow: &FunctionFlow, start: usize) -> (TraceStop, Vec<usize>) {
    let mut visited: HashSet<usize> = HashSet::new();
    let mut path: Vec<usize> = Vec::new();
    let mut current = start;

    loop {
        if !visited.insert(current) {
            /* 成环：环内没有非 inplace 起点 ⇒ 无法恢复。
             * 注意：`a = a + 1` **不是**这条——那条的读参 `a` 由更早的
             * 非 inplace 写入（`0 -> a`）锚定，上溯会走到那条并停下。 */
            return (TraceStop::CycleWithoutAnchor { cycle: path.clone() }, path);
        }
        path.push(current);
        let node = &flow.nodes[current];
        /* 本条自己不是 inplace ⇒ 它是起点（上层调用者从 start 出发，
         * 第一条就可能是非 inplace，但那只在调用方逻辑里出现）。 */
        if current != start && !node.inplace {
            return (TraceStop::NonInplace(current), path);
        }
        /*
         * 沿**读参**上溯：找最近一条「写过这个读参」的指令。
         * 写参不参与上溯——那是本条自己造成的影响，不是它的来源。
         */
        let mut found: Option<usize> = None;
        let mut hit_global: Option<(usize, String)> = None;

        for read in &node.reads {
            if read.starts_with('/') {
                /* 绝对 key 被读：它是否「已修改」由 written_globals 判。 */
                if flow.written_globals.contains(read) {
                    hit_global = Some((current, read.clone()));
                    break;
                }
                continue;
            }
            for (index, candidate) in flow.nodes.iter().enumerate() {
                if index >= current || !candidate.writes.contains(read) {
                    continue;
                }
                if let Some((gi, key)) = hit_global.take() {
                    let _ = (gi, key);
                }
                found = Some(index);
            }
        }
        if let Some((instruction, key)) = hit_global {
            return (
                TraceStop::WrittenGlobal {
                    instruction,
                    key: key.clone(),
                },
                path,
            );
        }
        match found {
            Some(previous) => current = previous,
            None => return (TraceStop::FunctionEntry, path),
        }
    }
}

/// 分析一个函数。
pub fn analyze(flow: &FunctionFlow) -> Resumability {
    let mut replay_from: Vec<(usize, usize)> = Vec::new();

    for (index, node) in flow.nodes.iter().enumerate() {
        if !node.inplace {
            continue;
        }
        match trace_back(flow, index) {
            (TraceStop::NonInplace(anchor), _) => replay_from.push((index, anchor)),
            (TraceStop::FunctionEntry, _) => {
                /* 起点是函数入口：入口本身没有指令可重放，
                 * 但整帧重放等价于从入口开始——判可恢复，起点记 0。 */
                replay_from.push((index, 0));
            }
            (TraceStop::WrittenGlobal { instruction, key }, chain) => {
                return Resumability::NotResumable {
                    instruction,
                    chain,
                    global_key: key,
                };
            }
            (TraceStop::CycleWithoutAnchor { .. }, chain) => {
                return Resumability::NotResumable {
                    instruction: index,
                    chain,
                    global_key: String::new(),
                };
            }
        }
    }

    if flow.nodes.iter().all(|n| !n.inplace) {
        Resumability::AlwaysResumable
    } else {
        Resumability::ConditionallyResumable { replay_from }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn node(id: usize, op: &str, reads: &[&str], writes: &[&str]) -> RwirNode {
        RwirNode {
            identifier: id,
            operator: op.to_string(),
            reads: reads.iter().map(|s| s.to_string()).collect(),
            writes: writes.iter().map(|s| s.to_string()).collect(),
            inplace: is_inplace_node(op, &reads.iter().map(|s| s.to_string()).collect::<Vec<_>>(),
                                     &writes.iter().map(|s| s.to_string()).collect::<Vec<_>>()),
        }
    }

    fn flow(nodes: Vec<RwirNode>, globals: &[&str]) -> FunctionFlow {
        FunctionFlow {
            name: "test".to_string(),
            nodes,
            written_globals: globals.iter().map(|s| s.to_string()).collect(),
        }
    }

    /// 三态之一：全函数无 inplace。
    #[test]
    fn always_resumable_without_inplace() {
        /* `copy` 本身在 `.def` 里也是 ALIAS_DECLARED（写参可与读参别名），
         * 所以「无 inplace」的用例要用纯计算算子。 */
        let f = flow(
            vec![
                node(0, "add", &["1", "2"], &["a"]),
                node(1, "mul", &["a", "1"], &["b"]),
            ],
            &[],
        );
        assert_eq!(analyze(&f), Resumability::AlwaysResumable);
    }

    /// 三态之二：有 inplace，但能上溯到非 inplace 起点。
    #[test]
    fn conditionally_resumable_traces_to_anchor() {
        /* 0: add 1,2 -> a                （非 inplace，锚点）
         * 1: compactarray·set(a, 0, 7)   （inplace，读 a 写 a）
         * ⇒ 上溯 1 的读参 a，最近写过 a 的是 0，且 0 非 inplace ⇒ 锚定 0。 */
        let f = flow(
            vec![
                node(0, "add", &["1", "2"], &["a"]),
                node(1, "compactarray·set", &["a"], &["a"]),
            ],
            &[],
        );
        match analyze(&f) {
            Resumability::ConditionallyResumable { replay_from } => {
                assert_eq!(replay_from, vec![(1, 0)]);
            }
            other => panic!("期望 CONDITIONALLY_RESUMABLE，得到 {:?}", other),
        }
    }

    /// 三态之三：上溯撞到已修改的全局路径。
    #[test]
    fn not_resumable_when_tracing_into_written_global() {
        /* 0: copy 1 -> /tmp/g   （写全局）
         * 1: compactarray·set(/tmp/g, 0, 7)  （inplace，读全局）
         * ⇒ 上溯撞上「已被写过的全局 key」⇒ 不可恢复。 */
        let f = flow(
            vec![
                node(0, "add", &["1", "2"], &["/tmp/g"]),
                node(1, "compactarray·set", &["/tmp/g"], &["/tmp/g"]),
            ],
            &["/tmp/g"],
        );
        match analyze(&f) {
            Resumability::NotResumable { global_key, .. } => {
                assert_eq!(global_key, "/tmp/g");
            }
            other => panic!("期望 NOT_RESUMABLE，得到 {:?}", other),
        }
    }

    /// **自环不是「已修改的全局路径」**：`a = a + 1` 要判可恢复。
    #[test]
    fn self_loop_is_not_a_written_global() {
        /* 0: add 0,0 -> a         （非 inplace 锚点）
         * 1: xv·set(a, ...)       （inplace，读写都是局部 a） */
        let f = flow(
            vec![
                node(0, "add", &["0", "0"], &["a"]),
                node(1, "xv·set", &["a"], &["a"]),
            ],
            &[],
        );
        assert!(
            matches!(analyze(&f), Resumability::ConditionallyResumable { .. }),
            "局部自环必须判可恢复"
        );
    }

    /// **`copy` 的收窄**：`b = a` 不是 inplace，`a = a` 才是。
    #[test]
    fn copy_is_inplace_only_when_slot_aliases() {
        assert!(!is_inplace_node("copy",
            &["a".to_string()], &["b".to_string()]),
            "b = a 读一处写另一处，不是 inplace");
        assert!(is_inplace_node("copy",
            &["a".to_string()], &["a".to_string()]),
            "a = a 同槽才是 inplace");
    }

    /// 判不出时保守按 inplace。
    #[test]
    fn unresolvable_aliasing_is_conservatively_inplace() {
        assert!(is_inplace_node("copy", &[], &["a".to_string()]));
        assert!(is_inplace_node("copy", &["a".to_string()], &[]));
    }

    /// inplace 表与 `.def` 的一致性由外部测试校验；这里钉住数量，
    /// 变了就必须同步改（**两份真相会漂移**）。
    #[test]
    fn inplace_operator_count_is_pinned() {
        assert_eq!(
            ALIAS_PERMITTING_OPERATORS.len(),
            9,
            "别名算子数量变了：请同步 rwir_catalog.def 的 ALIAS_DECLARED 行"
        );
    }
}

#[cfg(test)]
mod corpus_fixture_tests {
    /* 端到端夹具：**反例驱动**。
     *
     * 两条只差「写绝对 key」那一条：
     *   - `not_resumable.kv`        有写 ⇒ 上溯撞上已修改的全局路径 ⇒ NOT_RESUMABLE
     *   - `global_write_removed.kv` 无写 ⇒ 降级为可恢复
     *
     * 这不是恒真断言：删掉前者的那条写，它必须变。
     */
    use super::*;

    fn with_global_and_inplace(writes_global: bool) -> FunctionFlow {
        let mut nodes = vec![
            node_of(0, "compactarray·new", &[], &["a"]),
        ];
        if writes_global {
            nodes.push(node_of(1, "add", &["1", "2"], &["/tmp/cr_global"]));
        }
        /* inplace 指令**读的就是那个全局 key**：这才是「上溯撞上已修改的全局路径」。 */
        nodes.push(node_of(
            2,
            "compactarray·set",
            if writes_global { &["/tmp/cr_global"] } else { &["a"] },
            if writes_global { &["/tmp/cr_global"] } else { &["a"] },
        ));
        FunctionFlow {
            name: "test".to_string(),
            nodes,
            written_globals: if writes_global {
                ["/tmp/cr_global".to_string()].into_iter().collect()
            } else {
                Default::default()
            },
        }
    }

    fn node_of(id: usize, op: &str, reads: &[&str], writes: &[&str]) -> RwirNode {
        let r: Vec<String> = reads.iter().map(|s| s.to_string()).collect();
        let w: Vec<String> = writes.iter().map(|s| s.to_string()).collect();
        RwirNode {
            identifier: id,
            operator: op.to_string(),
            inplace: is_inplace_node(op, &r, &w),
            reads: r,
            writes: w,
        }
    }

    /// 夹具 1：写绝对 key 且被 inplace 读 ⇒ NOT_RESUMABLE。
    #[test]
    fn fixture_not_resumable() {
        let r = analyze(&with_global_and_inplace(true));
        assert_eq!(r.name(), "NOT_RESUMABLE", "有全局写必须判不可恢复");
    }

    /// 夹具 2：**同一函数删掉那条写** ⇒ 判据必须变（反例驱动，证明不是恒假）。
    #[test]
    fn fixture_global_write_removed_downgrades() {
        let r = analyze(&with_global_and_inplace(false));
        assert_ne!(r.name(), "NOT_RESUMABLE", "删掉全局写后不应再判不可恢复");
    }
}
