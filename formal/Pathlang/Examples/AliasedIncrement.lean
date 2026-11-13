import Pathlang.Crash

/-!
# The aliased increment `i + 1 -> i`

Paper §4 (Theorem 4) names `i + 1 -> i` as a step for which
A0 fails, and proposes lowering it through a temporary. This file checks both
claims on a toy machine whose state is a counter and two variables. The toy is
not the language's layout; it keeps only what the argument uses: a step reads
the counter, reads its operand, writes its result, and writes the counter last.

* `incr_not_A0`: a stop after the slot write and before the counter write
  leaves a store from which re-executing the step adds again.
* `incr_not_warFree`: at that cut the step reads `i` before writing it, so
  `replay` does not apply.
* `lowered_A0`: after lowering to `i + 1 -> t; t -> i`, every stop before a
  counter write is harmless, from every store.
-/

namespace Pathlang.Examples.AliasedIncrement

/-- The toy machine's keys: the counter and two variables. -/
inductive Key where
  | pc | i | t
  deriving DecidableEq, Repr

/-- Steps of the toy machine. They take no external inputs and emit no events. -/
abbrev Step := Prog Key Nat Unit Unit Unit

/-- The input stream; unused. -/
def ι : Nat → Unit := fun _ => ()

/-- `i + 1 -> i` at instruction 0. -/
def incr : Step := do
  let pc ← Prog.rd .pc
  match pc with
  | some 0 =>
    let i ← Prog.rd .i
    Prog.wr (.put .i (i.getD 0 + 1))
    Prog.wr (.put .pc 1)
  | _ => pure ()

/-- Counter at instruction 0 and `i = 0`. -/
def T₀ : Store Key Nat
  | .pc => some 0
  | .i => some 0
  | .t => none

/-- From `T₀` the step reads the counter and `i`, writes `i`, and then writes
the counter. -/
theorem incr_acts : (exec incr ι T₀).2 =
    [.read .pc (some 0), .read .i (some 0), .write (.put .i 1), .write (.put .pc 1)] :=
  rfl

theorem incr_after : after incr ι T₀ .i = some 1 := rfl

/-- The store a stop leaves after the slot write and before the counter write. -/
def T₁ : Store Key Nat := applyAll T₀ [.put .i 1]

theorem incr_crash : CrashAt T₀ (exec incr ι T₀).2 3 T₁ :=
  fun _ => Or.inl rfl

/-- Re-executing from that store adds again. -/
theorem incr_replay : after incr ι T₁ .i = some 2 := rfl

/-- **A0 fails for `i + 1 -> i`.** A stop leaves a store that still holds the
counter of `T₀`, and re-executing the step from it does not give the
uninterrupted result. -/
theorem incr_not_A0 :
    ∃ j T', CrashAt T₀ (exec incr ι T₀).2 j T' ∧ T' .pc = T₀ .pc ∧
      after incr ι T' ≠ after incr ι T₀ := by
  refine ⟨3, T₁, incr_crash, rfl, fun h => ?_⟩
  have h' := congrFun h .i
  rw [incr_replay, incr_after] at h'
  simp at h'

/-- At that cut the step reads `i` before writing it, so it is not
write-after-read free and `replay` does not apply. -/
theorem incr_not_warFree : ¬ WarFreeAt (exec incr ι T₀).2 3 := by
  intro h
  have h' := h .i rfl
  exact absurd h' (by decide)

/-! ## Lowering through a temporary -/

/-- The same update lowered to `i + 1 -> t` at instruction 0 and `t -> i` at
instruction 1. -/
def lowered : Step := do
  let pc ← Prog.rd .pc
  match pc with
  | some 0 =>
    let i ← Prog.rd .i
    Prog.wr (.put .t (i.getD 0 + 1))
    Prog.wr (.put .pc 1)
  | some 1 =>
    let t ← Prog.rd .t
    Prog.wr (.put .i (t.getD 0))
    Prog.wr (.put .pc 2)
  | _ => pure ()

theorem lowered_acts_0 (T : Store Key Nat) (h : T .pc = some 0) :
    (exec lowered ι T).2 =
      [.read .pc (some 0), .read .i (T .i), .write (.put .t ((T .i).getD 0 + 1)),
       .write (.put .pc 1)] := by
  simp [lowered, exec, h]

theorem lowered_acts_1 (T : Store Key Nat) (h : T .pc = some 1) :
    (exec lowered ι T).2 =
      [.read .pc (some 1), .read .t (T .t), .write (.put .i ((T .t).getD 0)),
       .write (.put .pc 2)] := by
  simp [lowered, exec, h]

theorem lowered_acts_other (T : Store Key Nat) (h0 : T .pc ≠ some 0) (h1 : T .pc ≠ some 1) :
    (exec lowered ι T).2 = [.read .pc (T .pc)] := by
  cases hpc : T .pc with
  | none => simp [lowered, exec, hpc]
  | some n =>
    match n with
    | 0 => exact absurd hpc h0
    | 1 => exact absurd hpc h1
    | n + 2 => simp [lowered, exec, hpc]

/-- A step that reads the counter and `x`, writes `y`, and then writes the
counter is write-after-read free at every cut before the counter write, provided
`y` is neither the counter nor `x`. -/
theorem warFree_of_shape {v u : Option Nat} {x y : Key} {w n : Nat}
    (hxy : x ≠ y) (hpy : Key.pc ≠ y) {j : Nat} (hj : j ≤ 3) :
    WarFreeAt ([.read .pc v, .read x u, .write (.put y w), .write (.put .pc n)] :
      List (Act Key Nat Unit Unit)) j := by
  intro k hk
  have hky : k ≠ y := by
    rintro rfl
    simp [Write.affects, hxy, hpy] at hk
  have hj' : j = 0 ∨ j = 1 ∨ j = 2 ∨ j = 3 := by omega
  rcases hj' with rfl | rfl | rfl | rfl <;>
    simp [cutKeys, headPartialKeys, Act.partialKeys, Write.affects, hky]

/-- **The lowered form satisfies A0.** From every store, every stop before the
step's counter write (cuts 0 to 3) is harmless: re-executing the step gives the
uninterrupted result. -/
theorem lowered_A0 (T T' : Store Key Nat) (j : Nat) (hj : j ≤ 3)
    (hc : CrashAt T (exec lowered ι T).2 j T') :
    after lowered ι T' = after lowered ι T := by
  refine (replay hc ?_).2
  match h : T .pc with
  | some 0 =>
    rw [lowered_acts_0 T h]
    exact warFree_of_shape (by decide) (by decide) hj
  | some 1 =>
    rw [lowered_acts_1 T h]
    exact warFree_of_shape (by decide) (by decide) hj
  | none =>
    rw [lowered_acts_other T (by simp [h]) (by simp [h])]
    exact warFreeAt_of_writes_nil rfl j
  | some (n + 2) =>
    rw [lowered_acts_other T (by simp [h]) (by simp [h])]
    exact warFreeAt_of_writes_nil rfl j

end Pathlang.Examples.AliasedIncrement
