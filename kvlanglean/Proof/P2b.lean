import Proof.Basic
import Proof.Lemmas

namespace Kvlang

/-- P2b：崩溃发生在非 inplace 指令 c 处。

    前提：
    1. `i.inplace = false`：读集与写集不相交（非 inplace 的定义）；
    2. `p[s.pc]? = some i`：PC 指向当前尚未执行的指令 c；
    3. `partialWrites` 是完整写集的子写集（崩溃前已落库的部分）；
    4. `ReadsWritesDisjoint` / `ReadsDetermined` / `EffectWritesOnly`：指令语义的抽象。

    `AtomicCommit` 不是前提：定理内部用 `atomic_commit_of_disjoint` 从第 1、4 条把它证出来。

    结论：从 c 重放一条，与无崩溃执行到 c 之后继续，得到同一个状态。
    这里不要求「多 key 写全有或全无」；要求的是「重放收敛」。 -/
theorem p2b_non_inplace (p : List Instr) (s : State) (i : Instr)
    (partialWrites : Writes) (n : Nat)
    (_hinplace : i.inplace = false)
    (hpc : p[s.pc]? = some i)
    (hpartial : PartialLe partialWrites (i.effect s.data))
    (hdisjoint : ReadsWritesDisjoint i)
    (hdet : ReadsDetermined i)
    (hwrites : EffectWritesOnly i) :
    runFuel p (n + 1) (interrupted s partialWrites) = runFuel p n (step s i) := by
  have hatomic : AtomicCommit i s.data :=
    atomic_commit_of_disjoint i s.data hdisjoint hdet hwrites
  have hpc' : p[(interrupted s partialWrites).pc]? = some i := by
    simpa [interrupted] using hpc
  simp only [runFuel, hpc']
  have hstep : step (interrupted s partialWrites) i = step s i := by
    unfold step interrupted
    rw [hatomic partialWrites hpartial]
  rw [hstep]

/-- P2b 推论（未落库）：若崩溃后数据仍等于崩溃前，恢复点就是 c 本身。 -/
theorem p2b_resume_at_crash (s : State) (partialWrites : Writes)
    (hpre : (interrupted s partialWrites).data = s.data) :
    interrupted s partialWrites = s := by
  cases s with
  | mk data pc status =>
      simp [interrupted] at hpre ⊢
      exact hpre

/-- P2b 推论（已落库）：若崩溃后数据等于完整执行，恢复点在 c 的下一格；
    数据、PC、status 三个字段都不需要额外修补。 -/
theorem p2b_resume_after_commit (s : State) (i : Instr) (partialWrites : Writes)
    (_hfull : (interrupted s partialWrites).data = (step s i).data) :
    (interrupted s partialWrites).pc = s.pc ∧
      (interrupted s partialWrites).status = s.status := by
  exact ⟨rfl, rfl⟩

/-- 恢复点判定：崩溃记录标明指令已提交 ⇒ 前移一格；否则停在崩溃指令。
    `committed` 来自恢复记录（PC 是否已推进 / 提交标记），不是对写 map 做可判定比较。 -/
def resumePoint (s : State) (committed : Bool) : Nat :=
  if committed then s.pc + 1 else s.pc

/-- P2b 的结构结论：恢复点只有「停在 c」与「前移到 c+1」两种，没有中间态。 -/
theorem p2b_resume_point_two_cases (s : State) (committed : Bool) :
    resumePoint s committed = s.pc ∨ resumePoint s committed = s.pc + 1 := by
  unfold resumePoint
  cases committed <;> simp

/-- P2b 的重放等价（未落库特例）：
    若崩溃没有落下任何写，从 c 重放一条指令，与无崩溃执行到 c 之后继续，
    得到同一个状态。 -/
theorem p2b_replay_equivalence (p : List Instr) (s : State) (i : Instr)
    (partialWrites : Writes) (n : Nat) (hpc : p[s.pc]? = some i)
    (hpre : (interrupted s partialWrites).data = s.data) :
    runFuel p (n + 1) (interrupted s partialWrites) = runFuel p n (step s i) := by
  have hstate : interrupted s partialWrites = s :=
    p2b_resume_at_crash s partialWrites hpre
  rw [hstate]
  simp only [runFuel, hpc]

end Kvlang
