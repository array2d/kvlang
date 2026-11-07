import Proof.Basic

namespace Kvlang

/-! ## 写集应用的基本性质 -/

/-- 若 `w'` 是 `w` 的子写集，先在 `w'` 上应用再应用完整 `w`，
    与直接应用完整 `w` 相同：已落库的子写集会被完整写集覆盖成同一个最终值。 -/
theorem applyWrites_apply_of_le (s : Store) (w' w : Writes) (h : PartialLe w' w) :
    applyWrites (applyWrites s w') w = applyWrites s w := by
  funext k
  by_cases hw : w k = none
  · have hw' : w' k = none := by
      cases h' : w' k with
      | none => rfl
      | some v =>
          have hle : w k = some v := h k v h'
          rw [hle] at hw
          cases hw
    simp [applyWrites, hw, hw']
  · cases hwv : w k with
    | none => exact False.elim (hw hwv)
    | some v => simp [applyWrites, hwv]

/-! ## 指令级原子性：从「读写不相交」推出来，不是前提 -/

/-- 非 inplace 的定义：读集与写集不相交。 -/
def ReadsWritesDisjoint (i : Instr) : Prop :=
  ∀ k, k ∈ i.reads → k ∉ i.writes

/-- 指令效果只由读集决定；读集相同则算出的效果相同。 -/
def ReadsDetermined (i : Instr) : Prop :=
  ∀ s₁ s₂, (∀ k ∈ i.reads, s₁ k = s₂ k) → i.effect s₁ = i.effect s₂

/-- 指令效果只会写到写集里的 key。 -/
def EffectWritesOnly (i : Instr) : Prop :=
  ∀ s k v, i.effect s k = some v → k ∈ i.writes

/-- 指令级原子性的可重放形态：任意崩溃子写集后重放该指令，
    都到达「完整执行一次」的同一个状态。 -/
def AtomicCommit (i : Instr) (s : Store) : Prop :=
  ∀ w', PartialLe w' (i.effect s) →
    applyWrites (applyWrites s w') (i.effect (applyWrites s w')) =
      applyWrites s (i.effect s)

/-- 读写不相交 ⇒ 子写集不改任何读参 ⇒ 重放算出的效果与首次完全一致。 -/
theorem effect_partial_eq (i : Instr) (s : Store) (w' : Writes)
    (hdisjoint : ReadsWritesDisjoint i)
    (hdet : ReadsDetermined i)
    (hwrites : EffectWritesOnly i)
    (hpartial : PartialLe w' (i.effect s)) :
    i.effect (applyWrites s w') = i.effect s := by
  apply hdet
  intro k hk
  have hnone : w' k = none := by
    cases h' : w' k with
    | none => rfl
    | some v =>
        have hle : i.effect s k = some v := hpartial k v h'
        have hw : k ∈ i.writes := hwrites s k v hle
        exact False.elim (hdisjoint k hk hw)
  simp [applyWrites, hnone]

/-- 指令级原子性：读集与写集不相交（非 inplace 定义）+ 效果只依赖读集
    + 效果只写写集里的 key ⇒ 任意崩溃子写集后重放都收敛到完整提交。
    单 key 写原子由 `applyWrites` 的逐 key 语义直接给出（引理 1）；
    这里证的是它如何提升到整条指令。 -/
theorem atomic_commit_of_disjoint (i : Instr) (s : Store)
    (hdisjoint : ReadsWritesDisjoint i)
    (hdet : ReadsDetermined i)
    (hwrites : EffectWritesOnly i) :
    AtomicCommit i s := by
  intro w' hpartial
  rw [effect_partial_eq i s w' hdisjoint hdet hwrites hpartial]
  exact applyWrites_apply_of_le s w' (i.effect s) hpartial

/-! ## 引理 3：重放确定 -/

/-- 同一个初始状态、同一段程序，重放结果唯一。
    实质前提是「重放区间的初始状态与崩溃前一致」，
    由 P2c 的静态上溯/保守过近似判定提供。 -/
theorem lemma3_replay_deterministic (p : List Instr) (n : Nat) :
    ∀ (s₁ s₂ : State), s₁ = s₂ → runFuel p n s₁ = runFuel p n s₂ := by
  intro s₁ s₂ h
  rw [h]

/-- 执行可分段：先跑 n 步再跑 m 步，等于一次跑 n + m 步。
    这是 P2a「任意指令边界暂停」的语义基础。 -/
theorem runFuel_add (p : List Instr) (n m : Nat) (s : State) :
    runFuel p (n + m) s = runFuel p m (runFuel p n s) := by
  induction n generalizing s with
  | zero => simp [runFuel]
  | succ n ih =>
      cases h : p[s.pc]? with
      | none =>
          cases m with
          | zero => simp [runFuel, h]
          | succ m => simp [Nat.succ_add, runFuel, h]
      | some i =>
          simp only [Nat.succ_add, runFuel, h]
          exact ih (step s i)

/-! ## 引理 2：上溯终止 -/

/-- 依赖图：`prev c = some r` 表示指令 c 的读参的最上游写入者是指令 r。 -/
abbrev DepGraph := Nat → Option Nat

/-- 上溯：每一步跳到更早的写入者；遇到 `stop`（已修改的全局路径 /
    layout 判定的截断点）或到达 `start`（函数入口 / vthread 起点）时停止。
    `start` 与 `stop` 是显式的循环边界，避免自环导致定义不终止。 -/
def traceback (prev : DepGraph) (start : Nat) (stop : Nat → Bool) :
    Nat → Nat → List Nat
  | 0, c => [c]
  | n + 1, c =>
      if stop c then [c]
      else
        match prev c with
        | none => [c]
        | some r => if r < start then [c] else c :: traceback prev start stop n r

/-- 上溯的局部递减性：在函数入口之后、且未被截断的节点，依赖边指向更早的指令。
    来源是 layout 的三态分析：循环内的 inplace 被判 `NotResumable`，不进入本定理；
    可恢复函数里，环在函数入口（`start`）或已修改的全局路径（`stop`）处截断。 -/
def TracebackDecreasing (prev : DepGraph) (start : Nat) (stop : Nat → Bool) : Prop :=
  ∀ c, start ≤ c → stop c = false → ∀ r, prev c = some r → r < c

/-- 引理 2a：每一步最多消耗一格燃料，所以结果长度 ≤ fuel + 1。 -/
theorem traceback_length_le_fuel (prev : DepGraph) (start : Nat) (stop : Nat → Bool) :
    ∀ (n c : Nat), (traceback prev start stop n c).length ≤ n + 1 := by
  intro n
  induction n with
  | zero =>
      intro _c
      simp [traceback]
  | succ n ih =>
      intro c
      simp only [traceback]
      split
      · simp
      · split
        · simp
        · split <;> simp [ih]

/-- 引理 2b：在 `TracebackDecreasing` 下，上溯至多走 `c - start + 1` 步。
    自环（`a = a + 1`）不满足 `TracebackDecreasing`；按裁决它会被 layout 判
    `NotResumable`，根本不进入本定理。 -/
theorem traceback_length_le_span (prev : DepGraph) (start : Nat) (stop : Nat → Bool)
    (h : TracebackDecreasing prev start stop) :
    ∀ (c n : Nat), start ≤ c →
      (traceback prev start stop n c).length ≤ c - start + 1 := by
  intro c n
  induction n generalizing c with
  | zero =>
      intro _hc
      simp [traceback]
  | succ n ih =>
      intro hc
      simp only [traceback]
      by_cases hstop : stop c = false
      · simp only [hstop, if_false]
        cases hprev : prev c with
        | none => simp
        | some r =>
            simp only [hprev]
            by_cases hrs : r < start
            · simp [hrs]
            · simp only [hrs, if_false, List.length_cons]
              have hsr : start ≤ r := by omega
              have hlt : r < c := h c hc hstop r hprev
              have ihr := ih r hsr
              omega
      · have hstop_true : stop c = true := by
          cases h : stop c with
          | false => exact False.elim (hstop h)
          | true => rfl
        simp [hstop_true]

end Kvlang
