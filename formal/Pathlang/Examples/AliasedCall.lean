import Pathlang.Crash

/-!
# Aliasing through parameter pointers

The callee `rwfunc inc(a) -> (c) { a + 1 -> c }` reads slot `a` and writes slot
`c`, two different keys, so a check on the instruction alone accepts it. A call
fills each parameter slot with a pointer to the actual argument (paper §3,
`bind`), so the call `inc(x) -> (x)` makes both operands resolve to `x`, and the
step is the aliased increment again. Write-after-read freedom therefore has to
be judged on resolved paths, and a lowering that only renames inside the callee
does not establish it.

The toy keeps the callee frame's two slots, two caller variables, and the
counter; pointer resolution is one hop.

* `aliased_not_A0`: with both slots pointing at `x`, a stop after the result
  write and before the counter write fails A0.
* `aliased_not_warFree`: the conflict is on the resolved key `x`, not on the
  slots.
* `distinct_A0`: with the slots pointing at different variables, as for
  `inc(x) -> (y)`, every stop before the counter write is harmless.
-/

namespace Pathlang.Examples.AliasedCall

/-- The counter, the callee's read slot `a` and write slot `c`, and two caller
variables. -/
inductive Key where
  | pc | a | c | x | y
  deriving DecidableEq, Repr

/-- Numbers and pointers to keys. -/
inductive Val where
  | num (n : Nat)
  | ptr (k : Key)
  deriving DecidableEq, Repr

abbrev Step := Prog Key Val Unit Unit Unit

/-- The input stream; unused. -/
def ι : Nat → Unit := fun _ => ()

/-- Follow the pointer held by a parameter slot. -/
def deref (k : Key) : Prog Key Val Unit Unit (Option Key) := do
  let v ← Prog.rd k
  match v with
  | some (.ptr p) => pure (some p)
  | _ => pure none

/-- The callee's instruction `a + 1 -> c` at counter 0 of its frame: resolve
both operands through their slots, read the source, write the destination, then
write the counter. -/
def addOne : Step := do
  let pc ← Prog.rd .pc
  match pc with
  | some (.num 0) =>
    let src ← deref .a
    let dst ← deref .c
    match src, dst with
    | some s, some d =>
      let v ← Prog.rd s
      match v with
      | some (.num n) =>
        Prog.wr (.put d (.num (n + 1)))
        Prog.wr (.put .pc (.num 1))
      | _ => pure ()
    | _, _ => pure ()
  | _ => pure ()

/-- The frame bound by `inc(x) -> (x)`: both slots point at `x`, and `x = 0`. -/
def aliased : Store Key Val
  | .pc => some (.num 0)
  | .a => some (.ptr .x)
  | .c => some (.ptr .x)
  | .x => some (.num 0)
  | .y => none

theorem aliased_acts : (exec addOne ι aliased).2 =
    [.read .pc (some (.num 0)), .read .a (some (.ptr .x)), .read .c (some (.ptr .x)),
     .read .x (some (.num 0)), .write (.put .x (.num 1)), .write (.put .pc (.num 1))] :=
  rfl

/-- The store a stop leaves after the result write and before the counter write. -/
def aliasedCrash : Store Key Val := applyAll aliased [.put .x (.num 1)]

theorem aliased_crash : CrashAt aliased (exec addOne ι aliased).2 5 aliasedCrash :=
  fun _ => Or.inl rfl

theorem aliased_after : after addOne ι aliased .x = some (.num 1) := rfl

theorem aliased_replay : after addOne ι aliasedCrash .x = some (.num 2) := rfl

/-- **A0 fails for the call `inc(x) -> (x)`**, although the callee's operand
slots are different keys. -/
theorem aliased_not_A0 :
    ∃ j T', CrashAt aliased (exec addOne ι aliased).2 j T' ∧ T' .pc = aliased .pc ∧
      after addOne ι T' ≠ after addOne ι aliased := by
  refine ⟨5, aliasedCrash, aliased_crash, rfl, fun h => ?_⟩
  have h' := congrFun h .x
  rw [aliased_replay, aliased_after] at h'
  simp at h'

/-- The step reads the resolved key `x` before writing it. -/
theorem aliased_not_warFree : ¬ WarFreeAt (exec addOne ι aliased).2 5 := by
  intro h
  have h' := h .x rfl
  exact absurd h' (by decide)

/-- **With distinct actual arguments the call satisfies A0.** From every store
whose frame binds `a` to `x` and `c` to `y`, every stop before the counter write
(cuts 0 to 5) is harmless. -/
theorem distinct_A0 (T T' : Store Key Val) (n j : Nat) (hj : j ≤ 5)
    (hpc : T .pc = some (.num 0)) (ha : T .a = some (.ptr .x))
    (hcy : T .c = some (.ptr .y)) (hx : T .x = some (.num n))
    (hc : CrashAt T (exec addOne ι T).2 j T') :
    after addOne ι T' = after addOne ι T := by
  have hacts : (exec addOne ι T).2 =
      [.read .pc (some (.num 0)), .read .a (some (.ptr .x)), .read .c (some (.ptr .y)),
       .read .x (some (.num n)), .write (.put .y (.num (n + 1))),
       .write (.put .pc (.num 1))] := by
    simp [addOne, deref, exec, hpc, ha, hcy, hx]
  refine (replay hc ?_).2
  rw [hacts]
  intro k hk
  have hky : k ≠ .y := by
    rintro rfl
    simp [Write.affects] at hk
  have hj' : j = 0 ∨ j = 1 ∨ j = 2 ∨ j = 3 ∨ j = 4 ∨ j = 5 := by omega
  rcases hj' with rfl | rfl | rfl | rfl | rfl | rfl <;>
    simp [cutKeys, headPartialKeys, Act.partialKeys, Write.affects, hky]

end Pathlang.Examples.AliasedCall
