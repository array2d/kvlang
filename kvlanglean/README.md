# kvlang 崩溃恢复契约：Lean 4 形式化（#572 P2a/P2b）

本目录是 #572「P2 崩溃恢复契约」的第一步：只证 **P2a（stop/resume）**、**P2b（crash 在非 inplace 指令处）** 与三条引理。
P2c（crash 在 inplace 指令处、沿依赖图上溯）依赖 coding3 的 layout 三态分析与保守过近似（写集含绝对 key 即视为可能已修改，判不出即 `NotResumable`），按设计稿等前置落地后再写。

设计依据：`workspace/622/looker1/inplace-and-recovery-design.md`。
模型 ↔ 实现的逐项对应、假设与未证部分见 `CORRESPONDENCE.md`。

## 构建

```bash
cd kvlanglean
lake build
```

工具链：`leanprover/lean4:v4.8.0`。

**Mathlib 决策（2026-11-07 架构师裁定）**：不接入，保持零第三方依赖。#572 写的是「不引第三方证明（Mathlib 标准）」——Mathlib 是允许、不是必需；本批只用 `List` / `Nat` / `omega` 就能证完，离线可复现比「用了 Mathlib」更有价值。`lakefile.lean` 里保留 `require mathlib` 的注释位置，不删。

## 模块

| 文件 | 内容 |
|---|---|
| `Proof/Basic.lean` | kvspace 数据面、指令、状态、执行、暂停/恢复、崩溃中断 |
| `Proof/Lemmas.lean` | 引理 1（单 key 写原子）、引理 2（上溯终止）、引理 3（重放确定）、执行分段 |
| `Proof/P2a.lean` | stop/resume 语义不变 |
| `Proof/P2b.lean` | 非 inplace 指令崩溃恢复 |
| `Proof.lean` | 汇总入口 |

## 结论

- `lake build` 通过，**无 `sorry`**。
- P2a：`p2a_stop_resume`、`p2a_boundary_decomposition`。
- P2b：`p2b_non_inplace`、`p2b_resume_at_crash`、`p2b_resume_after_commit`、`p2b_resume_point_two_cases`、`p2b_replay_equivalence`。
- 引理：`lemma1_single_key_write_atomic`、`atomic_commit_of_disjoint`、`traceback_length_le_fuel` / `traceback_length_le_span`、`lemma3_replay_deterministic`。

## 关键前提（写进定理，不是隐含假设）

1. **上溯终止**：`traceback_length_le_span` 显式要求 `TracebackDecreasing prev start stop`。它不是笼统的「图无环」，而是「上溯在函数入口（`start`）或已修改的全局路径（`stop`）处截断，且未截断的依赖边指向更早的指令」。循环内的 inplace 被 layout 判 `NotResumable`，不进入本定理；自环（`a = a + 1`）因此不是本定理的反例，而是被三态分析挡在门外。
2. **非 inplace 指令原子**：`AtomicCommit` 不是公理，也不是 `p2b_non_inplace` 的前提——定理内部用 `atomic_commit_of_disjoint` 从「读集与写集不相交（非 inplace 定义）+ 效果只依赖读集 + 效果只写写集」把它证出来。单 key 原子由 P1 给出（`lemma1`）。
3. **重放确定**：`lemma3_replay_deterministic` 只要求初始状态相等；真正的实现前提「重放区间未读已修改的全局状态」属于 P2c，尚未落地。
4. **暂停只改控制面**：`pause` 只改 `status`，`runFuel` 不读 `status`；实现必须保证指令内部不按 status 分叉。
5. **PC 约定**：`pc` 指向当前尚未执行的那条指令。崩溃落在 c 时 `pc` 仍指向 c，恢复从 c 重放；实现里若 `‥pc` 已推进到 c+1，「PC-1」就是本模型的 c。

## 不证什么

- XValue wire 的字节布局与编解码（P1）；
- runtimec / vmbin / JIT 的 C 实现（本模型是抽象解释器；对齐靠 tutorial/benchmark 对拍）；
- inplace 的 layout 静态校验与依赖图构造（coding3）；
- P2c 的静态上溯与保守过近似判定（写集含绝对 key ⇒ 可能已修改；判不出 ⇒ `NotResumable`）；本批只把它写成 `TracebackDecreasing` 的来源前提，不证判定器本身；
- 多 vthread 并发。
