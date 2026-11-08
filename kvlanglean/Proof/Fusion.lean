/-
  融合步（Δ>1）的语义骨架：T1/T2/T3。

  这批只写命题与前提，不补证明；每个 `sorry` 在注释里写清缺什么。
  C1/C1*/C2/C3 的对应：
  - C1  = `BlockNonInplace`（每条指令 `ReadsWritesDisjoint`，见 Proof/Lemmas.lean）
  - C1* = `LaterWriteRecovered`（后向读必须被更早写恢复）
  - C1' = `NoFutureRead`（保守近似：完全不读更晚写的 key）
  - C2  = `ExternalReadsStable`（块内未写的 key，崩溃态与初始态一致）
  - C3  = `CommitOrdered`（一次 pc 推进、效果先于提交、8 字节对齐原子写）
-/

import Proof.Basic
import Proof.Lemmas

namespace Kvlang

/-- 融合块：覆盖的明文指令序列 + Δ（Δ = instrs.length）。 -/
structure Block where
  instrs : List Instr
  delta : Nat

/-- 块顺序执行：按序应用每条指令的效果。 -/
def runBlock (b : Block) (s : Store) : Store :=
  b.instrs.foldl (fun st i => applyWrites st (i.effect st)) s

/-- 从 s 执行块内前 j 条指令后的状态。 -/
def prefixState (b : Block) (j : Nat) (s : Store) : Store :=
  (b.instrs.take j).foldl (fun st i => applyWrites st (i.effect st)) s

/-- 崩溃可达态：
    1. 执行到第 j 条时崩溃，当前指令只落库子写集 `w'`；
    2. 全部效果已落库，但 `atomicincr` 尚未提交。
    注意不能用「最终写 map 的任意子集」代替：块内不同指令可能写同一个 key，
    前缀留下的旧值不等于最终值。 -/
def Reachable (b : Block) (s p : Store) : Prop :=
  (∃ (j : Nat) (i : Instr), j < b.instrs.length ∧ b.instrs[j]? = some i ∧
      ∃ w' : Writes, PartialLe w' (i.effect (prefixState b j s)) ∧
        p = applyWrites (prefixState b j s) w')
  ∨ p = prefixState b b.instrs.length s

/-- 块重放原子性：任意崩溃态重放整块，都回到完整执行一次的状态。 -/
def StepReplayAtomic (b : Block) (s : Store) : Prop :=
  ∀ p, Reachable b s p → runBlock b p = runBlock b s

/-! ## C1 / C1* / C1' / C2 / C3 -/

/-- C1：块内每条指令非 inplace（读集与自己的写集不相交）。 -/
def BlockNonInplace (b : Block) : Prop :=
  ∀ i ∈ b.instrs, ReadsWritesDisjoint i

/-- C1'：保守近似——一条指令不读「块内更晚指令会写」的 key。 -/
def NoFutureRead (b : Block) : Prop :=
  ∀ (j : Nat) (i : Instr), b.instrs[j]? = some i →
    ∀ (t : Nat) (i' : Instr), j < t → b.instrs[t]? = some i' →
      ∀ k, k ∈ i.reads → k ∉ i'.writes

/-- C1*：精确充分条件——若第 j 条读到的 key 会被块内更晚指令写，
    则块内必须有一条更早的指令也写它（重放时先恢复原值）。 -/
def LaterWriteRecovered (b : Block) : Prop :=
  ∀ (j : Nat) (i : Instr), b.instrs[j]? = some i →
    ∀ k, k ∈ i.reads →
      (∃ (t : Nat) (i' : Instr), j < t ∧ b.instrs[t]? = some i' ∧ k ∈ i'.writes) →
      (∃ (u : Nat) (i' : Instr), u < j ∧ b.instrs[u]? = some i' ∧ k ∈ i'.writes)

/-- C2：块内未写的 key，在任何崩溃态里的值都与初始态一致
    （即块执行期间没有外部写者改它）。读到绝对 key 且可能被外部修改时，
    layout/compiler 的保守过近似应判 `NotResumable`。 -/
def ExternalReadsStable (b : Block) (s : Store) : Prop :=
  ∀ p, Reachable b s p → ∀ k, (∀ i ∈ b.instrs, k ∉ i.writes) → p k = s k

/-- C3：提交顺序前提。
    `delta_eq` 是编译器必须保证的 Δ = 指令条数；
    其余字段是执行器/编译器约束，当前模型不展开（Store 模型里没有 pc）。 -/
structure CommitOrdered (b : Block) : Prop where
  delta_eq : b.delta = b.instrs.length
  effectsBeforeCommit : True
  alignedAtomicPcWrite : True

/-! ## T1：融合块与 O0 序列观测等价 -/

/-- 把块当作一个宏步执行。 -/
def stepBlock (s : State) (b : Block) : State :=
  { data := runBlock b s.data, pc := s.pc + b.delta, status := s.status }

/-- O0 从本地 pc=0 顺序执行同一个明文指令列表。 -/
def runO0 (is : List Instr) (s : State) : State :=
  runFuel is is.length { s with pc := 0 }

/-- T1 `fusion_refines_sequence`：融合块的 data 结果与 O0 明文序列一致。

    **本轮不证**。缺口：需要一条索引引理，把 `runFuel` 的 pc 步进
    （`p[pc]?`，每步 pc+1）与 `List.foldl` 按序应用对齐；
    当前模型没有「pc 偏移 = 列表下标」的桥。
    下一步：等 coding5 的 K 扫描数据、把步定义改成「周期块」后一起补。 -/
theorem fusion_refines_sequence (b : Block) (s : State)
    (_hdelta : b.delta = b.instrs.length) :
    (stepBlock s b).data = (runO0 b.instrs s).data := by
  sorry

/-! ## T2：块级重放原子性 -/

/-- T2 `block_replay_atomic_of_closed`：C1 + C1* + C2 + C3 ⇒ 块重放原子性。

    **本轮不证**。缺口：
    1. 对 `b.instrs` 的归纳，证明「重放到第 j 条之前，状态已与原执行前缀一致」；
    2. 每条指令调用 `atomic_commit_of_disjoint`（需要 `ReadsDetermined` /
       `EffectWritesOnly`，见 kvlanglean/CORRESPONDENCE.md §4.1）；
    3. 从 C1* 推出「后向读的 key 已被更早写恢复」；
    4. 从 C2 推出「块内未写的 key 在重放期间不变」；
    5. 证明 `Reachable` 覆盖所有崩溃点（当前定义已覆盖，但还没证完备性）。
    下一步：等 coding5 的 K 扫描数据、把步定义改成「周期块」后一起补。 -/
theorem block_replay_atomic_of_closed (b : Block) (s : Store)
    (_hC1 : BlockNonInplace b)
    (_hC1star : LaterWriteRecovered b)
    (_hC2 : ExternalReadsStable b s)
    (_hC3 : CommitOrdered b) :
    StepReplayAtomic b s := by
  sorry

/-! ## T3：多执行体共享同一份步划分与提交点 -/

/-- 执行体：明文解释器、bin 解释器、JIT。 -/
inductive Executor where
  | vmkv
  | vmbin
  | jit

/-- 编译器产出的步起点表（s0 值）。 -/
abbrev StepStarts := List Nat

/-- 三个执行体对同一函数使用同一份步起点表。 -/
def CommitScheduleConsistent (S : Executor → StepStarts) (starts : StepStarts) : Prop :=
  ∀ e, S e = starts

/-- T3 `commit_schedule_consistency`：给定同一份步起点表，
    vmkv / vmbin / JIT 的提交点一致。

    **边界（显式未完成项）**：本定理只证「已知三个执行体拿到同一份 `starts` 时，
    它们的提交点一致」。`binrwircompiler` 是否真的对三个执行体产出同一份 `starts`
    **不在本骨架内**，也没有被证明。论文与报告不得把这条读成「三执行体同步已经证完」；
    它只是一个条件定理，前提 `CommitScheduleConsistent` 必须由 compiler 的实现/测试提供。 -/
theorem commit_schedule_consistency (S : Executor → StepStarts) (starts : StepStarts)
    (h : CommitScheduleConsistent S starts) :
    S .vmkv = S .vmbin ∧ S .vmbin = S .jit := by
  have hv : S .vmkv = starts := h .vmkv
  have hb : S .vmbin = starts := h .vmbin
  have hj : S .jit = starts := h .jit
  exact ⟨by rw [hv, hb], by rw [hb, hj]⟩

end Kvlang
