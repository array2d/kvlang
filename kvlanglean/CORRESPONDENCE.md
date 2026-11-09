# 模型 ↔ 实现对应说明

本文件说明 `kvlanglean/` 里的 Lean 抽象模型对应实现/spec 的哪一层、哪些是假设、哪些没有证明。
目标 issue：#572（P2 崩溃恢复契约），设计稿：`workspace/622/looker1/inplace-and-recovery-design.md`。

## 一、抽象与实现的逐项对应

| Lean 概念 | 实现 / spec | 说明 |
|---|---|---|
| `Key = String` | kvspace 路径（`/vlib/...`、帧槽、数据 key） | 不解析路径文法；路径只作为不透明 key。 |
| `Value = ByteArray` | XValue body（head/body/attr 的 body 部分） | 线格式由 P1 证明；本模型不展开字节布局。 |
| `Store = Key → Option Value` | kvspace 数据面 | `none` 表示 key 不存在；`some v` 表示完整值。 |
| `writeKey` | 单 key 写原语（含 `kvspaceWriteInPlace` 语义） | 抽象层把它当作一次原子动作；P1 负责「head+body+attr 整体可见」。 |
| `Writes = Key → Option Value` | 一条指令的写集 | `none` 表示不写该 key；逐 key 的匹配使「单 key 半写」不可能。 |
| `applyWrites` | 把写集应用到 kvspace | 写到的 key 取新值，没写到的保留旧值。 |
| `PartialLe` | 崩溃时已落库的子写集 | `w'` 的每个写都在完整写集 `w` 里且值相同。 |
| `AtomicCommit` | 非 inplace 指令的效果原子 | **不再当公理**：由 `atomic_commit_of_disjoint` 从读写不相交 + 效果只依赖读集 + 效果只写写集推出。 |
| `Instr.reads` / `Instr.writes` | BinRWIR 指令的读参 / 写参 | 对应 `instruction.read_arguments` / `write_arguments`。 |
| `Instr.inplace` | `inplace` 修饰符（设计稿 §2.3） | 本模型把它当输入布尔量；layout 的三条深度校验不在本批证明范围内。 |
| `Instr.effect` | 指令的值计算与待写值 | 抽象为 `Store → Writes`；本模型不证明值计算本身。 |
| `State.data` | kvspace 持久数据面 | 双栈里的 kv 栈。 |
| `State.pc` | `‥pc`（`[stackid,s0]`，`[2]int32`） | 抽象为「下一条待执行指令」的 `Nat` 下标；映射见第二节。 |
| `State.status` | `‥status`（`init/running/wait/paused/error`） | 词表与 spec 一致。 |
| `step` | 执行一条 BinRWIR 指令并提交 | 提交效果、PC 前进一格、status 不变。 |
| `runFuel` / `run` | 解释器执行循环 | `runFuel` 的燃料是显式的有界调度；`run` 给足 `p.length + 1`。 |
| `pause` / `resume` | `‥status = paused` → 进程退出 → 新进程恢复 | `pause` 只改 status；`resume` 只把 status 改回 `running`。 |
| `interrupted` | SIGKILL 落在指令 c 的中间 | PC 未前进，指令只落库子写集 `partialWrites`。 |
| `resumePoint` | 设计稿的恢复点判定（`PC-1` vs `PC`） | 抽象为「停在 c」或「前移到 c+1」两种；`committed` 来自恢复记录。 |
| `DepGraph` | layout 阶段的依赖图 | `prev c = some r`：指令 c 的读参最上游写入者是 r。 |
| `traceback` | 设计稿 §3.2 的「上溯」 | `start` 是函数入口 / vthread 起点；`stop` 是已修改的全局路径或 layout 截断点。 |
| `TracebackDecreasing` | layout 三态分析的可恢复判据 | 不是笼统「图无环」：未截断的依赖边必须指向更早指令；循环内 inplace 被 layout 判 `NotResumable`，不进入本定理。 |

## 二、PC 的两种视角

实现里的 `‥pc` 是 `[stackid,s0]`，并且可能在取指后已经前进。抽象模型采用**「`pc` 指向当前尚未执行的指令，崩溃不推进 `pc`」**这一约定，落在 `State.pc` 的 doc comment 与 `interrupted` 定义（pc 不变）里：

- **暂停点**：`‥pc` 指向下一条指令，抽象 `pc` 就是它；暂停不改 PC，恢复从同一下标继续。
- **崩溃点**：实现可能已经把 PC 推到 c+1，恢复规则因此说「从 PC-1 继续」。抽象模型直接把 `pc` 固定在 c（未提交指令），`interrupted` 不改 PC；两者描述的是同一个恢复点，`p2b_resume_at_crash` 与 `p2b_resume_after_commit` 都按这一约定写。

因此 `p2b_resumePoint` 的两种结果（停在 c / 前移到 c+1）分别对应实现的「回退到 c 重放」与「c 已提交，从 c+1 继续」。

## 三、定理与前提

| 定理 | 内容 | 前提 | 状态 |
|---|---|---|---|
| `lemma1_single_key_write_atomic` | 单 key 写集与 `writeKey` 等价 | P1 的 wire 原子性（模型里逐 key 匹配，不存在半写值） | 已证 |
| `atomic_commit_of_disjoint` | 读写不相交 + 效果只依赖读集 + 效果只写写集 ⇒ 任意子写集重放收敛 | 三条性质（非 inplace 定义、指令语义、写集形状） | 已证 |
| `traceback_length_le_fuel` | 上溯结果长度 ≤ 燃料 + 1 | 无 | 已证 |
| `traceback_length_le_span` | 未截断依赖边严格递减时，上溯至多 `c - start + 1` 步 | `TracebackDecreasing prev start stop`、`start ≤ c` | 已证 |
| `lemma3_replay_deterministic` | 同一初始状态重放结果唯一 | 初始状态相等（P2c 的静态上溯 / 保守过近似判定负责提供） | 已证 |
| `runFuel_add` | 先跑 n 步再跑 m 步 = 跑 n+m 步 | 无 | 已证 |
| `p2a_stop_resume` | 暂停后恢复执行与从未暂停等价 | 暂停前 `status = running` | 已证 |
| `p2a_boundary_decomposition` | 指令边界的暂停/恢复可分段 | 无 | 已证 |
| `p2b_non_inplace` | 非 inplace 指令崩溃后，从 c 重放一条 = 无崩溃执行到 c 之后继续 | `inplace = false`、PC 指向 c、子写集、读写不相交 + 效果只依赖读集 + 效果只写写集；`AtomicCommit` 在定理内部由上一行证出，不是前提 | 已证 |
| `p2b_resume_at_crash` | 未落库时恢复点就是 c | 崩溃后数据 = 崩溃前 | 已证 |
| `p2b_resume_after_commit` | 已落库时恢复点在 c+1，其它字段不需修补 | 崩溃后数据 = 完整执行后 | 已证 |
| `p2b_resume_point_two_cases` | 恢复点只有「停在 c / 前移到 c+1」 | 无 | 已证 |
| `p2b_replay_equivalence` | 未落库特例：从 c 重放一条 = 无崩溃执行到 c 之后继续 | PC 指向 c、崩溃后数据 = 崩溃前 | 已证 |

本批没有 `sorry`；`lake build` 通过。

## 四、建模了但没有证明的部分

- **wire 编解码与地址计算**：属 P1；本模型只把 `Value` 当不透明字节串，不证明 head/body/attr 布局。
- **`AtomicCommit` 的来源**：已由 `atomic_commit_of_disjoint` 证出，不再是前提。它的三条输入是：读集与写集不相交（非 inplace 定义）、`ReadsDetermined`（效果只由读集决定）、`EffectWritesOnly`（效果只写写集）。后两条是指令语义的抽象，不证。单 key 原子由 P1 给出。
- **写集的 map 抽象**：`Instr.effect` 抽象为 `Store → Writes`（key 到新值的部分映射），而不是有序写列表。真实 BinRWIR 的写参是有限集合；map 抽象丢弃了写顺序，但恢复性质只依赖「每个 key 的最终值」，所以不影响 P2a/P2b。
- **inplace 的 layout 静态校验**：设计稿 §2.3 的三条（读过、写过、同一指令既读又写）不在本批；模型只消费 `Instr.inplace` 布尔量。
- **依赖图的构造**：`prev` 是输入；layout 如何生成它（coding3 的工作）不在本批。
- **status 与执行的关系**：模型里 `runFuel` 不读 status，因此暂停只影响控制面。实现必须保证执行循环不在指令内部依据 status 改变数据路径；这是实现约束，不是本模型的定理。
- **多 vthread / 并发**：模型是单线程顺序执行；跨 vthread 的调度与并发写不在本批。

### 4.1 假设登记表（语义抽象）

`atomic_commit_of_disjoint` 不是从空气里证出来的：它依赖下面两条**建模假设**，外加 `ReadsWritesDisjoint`（非 inplace 的定义）。这两条不是公理，但报告读者必须能一眼看到「这个证明依赖了哪几条语义抽象」，以及将来怎么消掉。

| 名称 | 含义 | 将来怎么消掉 |
|---|---|---|
| `ReadsDetermined i` | 指令效果只由读集决定：`∀ s₁ s₂, (∀ k ∈ i.reads, s₁ k = s₂ k) → i.effect s₁ = i.effect s₂`。 | 建模 BinRWIR 的值计算与读参取数语义，从指令结构证明 `effect` 只读 `i.reads`；需要把 Value 的算子语义形式化（P1 只管线格式，不含算子）。在完成前，它是重放确定性证明的输入。 |
| `EffectWritesOnly i` | 指令效果写到的 key 必在 `i.writes` 里：`∀ s k v, i.effect s k = some v → k ∈ i.writes`。 | 建模写参形状：让 `effect` 由 `writes` 与值计算结构性生成，而不是任意函数；这样 key 属于 `writes` 是构造性质，不需要假设。 |

与这两条相邻、但不属于「定理假设」的建模选择：

- **写集用 map**：`Instr.effect : Store → Writes`，`Writes = Key → Option Value`；崩溃态是子写集 `PartialLe`。这是抽象选择，不是假设；它丢掉了写顺序，而 P2a/P2b 只依赖「每个 key 的最终值」。若将来要证依赖写顺序的性质，需要换回序列模型并补顺序引理。
- **单 key 原子**：由 P1 的 wire 模型给出；本模型以逐 key 的 `applyWrites` 承载，并用 `lemma1_single_key_write_atomic` 对齐到 `writeKey`。
- **`AtomicCommit`**：已不是假设，是 `atomic_commit_of_disjoint` 的结论；P2b 在定理内部调用它。
- **`TracebackDecreasing`**：定理前提，来源是 coding3 的 layout 三态分析；循环内 inplace 被 `NotResumable` 排除，不进入定理。
- **执行不读 `status`**：`runFuel` 模型不依赖 status；实现需要证明执行循环只在指令边界读 status。这是实现约束，不是数学假设。

## 五、P2c 为什么不在本批

P2c 要求：

1. P1 的 wire 可比性（判断某个槽是否被改过）；
2. coding3 的 layout 三态分析（`Resumable` / `NotResumable` / 上溯截断点）已经落地。

**阻塞不是「版本号」。** 已裁决不加数据 key 版本号；P2c 用**静态上溯 + 保守过近似**：

- 写集含绝对 key ⇒ 该 key 视为「可能已修改」；
- 判不出是否修改 ⇒ 该函数判 `NotResumable`，不进入 P2c 定理；
- 可恢复函数的上溯在函数入口或已修改的全局路径处截断——这正是本批 `TracebackDecreasing` 的 `start` / `stop` 两个参数。

因此本批只证 P2a/P2b 与三条引理；P2c 的定理陈述等 coding3 的三态分析落地后再写。`lemma3_replay_deterministic` 的前提「重放区间不读已修改的全局状态」将由该判定的保守过近似提供。

## 六、复现

```bash
cd kvlanglean
lake build
```

工具链：`leanprover/lean4:v4.8.0`。本批只使用 Lean 核心库（`List` / `Nat` / `Option` / `ByteArray`），未引入 Mathlib 或其它第三方证明。

**Mathlib 决策（2026-11-07 架构师裁定）**：不接入，保持零依赖。#572 的「不引第三方证明（Mathlib 标准）」是允许而非必需；本批只用核心库即可证完，离线可复现优先。`lakefile.lean` 里保留 `require mathlib` 的注释位置，不删。

## 七、融合步骨架与判据（2026-11-08 新增，本轮不证）

文件：`Proof/Fusion.lean`（模型与 T1/T2/T3）、`Proof/FusionTest.lean`（Lean 判据测试）；执行判据：`layout/src/fusion.rs` + `layout/tests/fusion_block_check.rs`。

### 7.1 定理与 sorry

| 定理 | 状态 | 缺口 / 下一步 |
|---|---|---|
| T1 `fusion_refines_sequence` | **本轮不证**（sorry） | 缺一条索引引理，把 `runFuel` 的 pc 步进与 `List.foldl` 按序应用对齐；当前模型没有「pc 偏移 = 列表下标」的桥。等 coding5 的 K 扫描数据、步定义改成「周期块」后再补。 |
| T2 `block_replay_atomic_of_closed` | **本轮不证**（sorry） | 缺：对 `instrs` 的归纳；逐指令调用 `atomic_commit_of_disjoint`（依赖 §4.1 的 `ReadsDetermined` / `EffectWritesOnly`）；从 C1* 推后向读恢复；从 C2 推未写 key 不变；`Reachable` 完备性。同样等「周期块」步定义。 |
| T3 `commit_schedule_consistency` | 已证（0 axiom） | **边界（显式未完成项）**：只证「给定同一份 `starts`，vmkv/vmbin/JIT 提交点一致」；`binrwircompiler` 是否对三执行体产出同一份 `starts` 不在骨架内，论文不得读成「三执行体同步已证完」。 |

C1 = `BlockNonInplace`；C1* = `LaterWriteRecovered`；C1'（保守近似）= `NoFutureRead`；C2 = `ExternalReadsStable`；C3 = `CommitOrdered`。C2/C3 目前是前提谓词：C3 的 `effectsBeforeCommit` / `alignedAtomicPcWrite` 在模型里是 `True` 占位，需在 runtime/编译器模型里细化。

### 7.2 可执行判据

- `layout/src/fusion.rs` 提供 `per_instruction_ok`（C1）、`no_future_read`（C1'）、`later_write_recovered`（C1*）、`block_admissible`（C1 ∧ C1*）。**任何融合实现都必须调用 `block_admissible`**，不满足即退回 Δ=1，不允许「尽力而为」。
- `layout/tests/fusion_block_check.rs` 用 `[b = a; a = 1]` 断言：逐指令检查放过、块级检查拒绝；另加正例 `[a = 1; b = a; a = 2]` 与 inplace 反例。运行：`cd layout && cargo test --test fusion_block_check`（当前 4/4 通过）。

### 7.3 未完成项清单

1. T1/T2 本轮不证；步定义等「周期块」重写后再补。
2. T3 的 compiler 侧 `starts` 产出未证。
3. C2/C3 的模型细化（外部读稳定的执行器判定、提交顺序的 pc/内存序语义）。
4. inplace 进融合块的切块规则（走 P2c 上溯）尚未接到本骨架。
