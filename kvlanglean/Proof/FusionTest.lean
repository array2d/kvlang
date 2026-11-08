/-
  判据测试：`[b = a; a = 1]`。

  逐指令检查（C1 / `BlockNonInplace`）会放过它：两条指令各自都非 inplace
  （i₁ 读 a 写 b；i₂ 只写 a）。
  块级检查（C1* / `LaterWriteRecovered`，以及保守的 C1' / `NoFutureRead`）会拒绝：
  i₁ 读到 a，而块内更晚的 i₂ 会写 a，且没有更早的写把 a 恢复成原值。
-/

import Proof.Fusion

namespace Kvlang

def keyA : Key := "a"
def keyB : Key := "b"

/-- i₁: `b = a`。 -/
def testInstr1 : Instr where
  reads := [keyA]
  writes := [keyB]
  inplace := false
  effect := fun s k => if k = keyB then s keyA else none

/-- i₂: `a = 1`（这里用空字节串代表常量 1；判据只关心 key 集合）。 -/
def testInstr2 : Instr where
  reads := []
  writes := [keyA]
  inplace := false
  effect := fun _ k => if k = keyA then some ByteArray.empty else none

def testBlock : Block where
  instrs := [testInstr1, testInstr2]
  delta := 2

/-- 逐指令检查放过：两条指令各自都非 inplace。 -/
example : BlockNonInplace testBlock := by
  intro i hi
  simp [testBlock] at hi
  rcases hi with rfl | rfl
  · intro k hk
    simp [testInstr1, ReadsWritesDisjoint] at hk ⊢
    rw [hk]
    decide
  · intro k hk
    simp [testInstr2, ReadsWritesDisjoint] at hk

/-- 块级精确判据拒绝：i₁ 的后向读没有被更早的写恢复。 -/
example : ¬ LaterWriteRecovered testBlock := by
  intro h
  have hread : keyA ∈ testInstr1.reads := by simp [testInstr1]
  have hlater : ∃ (t : Nat) (i' : Instr),
      (0 : Nat) < t ∧ testBlock.instrs[t]? = some i' ∧ keyA ∈ i'.writes := by
    exact ⟨1, testInstr2, by decide, by rfl, by simp [testInstr2]⟩
  rcases h 0 testInstr1 (by rfl) keyA hread hlater with
    ⟨u, _i', hu, _hi', _hmem⟩
  omega

/-- 保守近似 C1' 也拒绝：i₁ 读了更晚指令会写的 key。 -/
example : ¬ NoFutureRead testBlock := by
  intro h
  have hread : keyA ∈ testInstr1.reads := by simp [testInstr1]
  have := h 0 testInstr1 (by rfl) 1 testInstr2 (by decide)
    (by rfl) keyA hread
  simp [testInstr2] at this

end Kvlang
