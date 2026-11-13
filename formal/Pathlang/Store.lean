/-!
# Stores and store operations

A store is a flat partial map from keys to values (paper §2.1). Keys and
values are type parameters here; the language's paths and values instantiate
them once the specification fixes them.

The backend offers single-key reads and writes, each atomic. A step performs a
sequence of operations; `applyAll` applies a sequence. The basic fact the crash
proofs use is `applyAll_congr`: the result of a sequence depends on the
starting store only at keys the sequence does not touch. Last-writer-wins
(`applyAll_absorb`, `applyAll_take_absorb`) follows from it.
-/

namespace Pathlang

/-- A store: a partial function from keys to values. -/
abbrev Store (K V : Type) := K → Option V

/-- An operation the backend performs on a store. -/
inductive Write (K V : Type) where
  /-- Write `v` at `k`. Atomic. -/
  | put (k : K) (v : V)
  /-- Delete `k`. Atomic. -/
  | del (k : K)
  /-- Delete every key satisfying `P`, such as a frame subtree. Not atomic: a
  stop can leave any subset of these keys deleted (`Pathlang.CrashAt`). -/
  | delWhere (P : K → Bool)

namespace Write

variable {K V : Type}

/-- The value an affected key holds after the operation. -/
def newVal : Write K V → Option V
  | put _ v => some v
  | del _ => none
  | delWhere _ => none

variable [DecidableEq K]

/-- `w.affects k` holds iff `w` sets the value at `k`. -/
def affects : Write K V → K → Bool
  | put k _, k' => decide (k' = k)
  | del k, k' => decide (k' = k)
  | delWhere P, k' => P k'

/-- Apply one operation. -/
def apply (w : Write K V) (T : Store K V) : Store K V :=
  fun k => if w.affects k then w.newVal else T k

theorem apply_of_affects {w : Write K V} {k : K} (T : Store K V)
    (h : w.affects k = true) : w.apply T k = w.newVal := by
  simp [apply, h]

theorem apply_of_not_affects {w : Write K V} {k : K} (T : Store K V)
    (h : w.affects k = false) : w.apply T k = T k := by
  simp [apply, h]

end Write

variable {K V : Type} [DecidableEq K]

/-- Apply operations left to right. -/
def applyAll (T : Store K V) (ws : List (Write K V)) : Store K V :=
  ws.foldl (fun S w => w.apply S) T

/-- Some operation in `ws` affects `k`. -/
def touches (ws : List (Write K V)) (k : K) : Bool :=
  ws.any (fun w => w.affects k)

@[simp] theorem applyAll_nil (T : Store K V) : applyAll T [] = T := rfl

@[simp] theorem applyAll_cons (T : Store K V) (w : Write K V) (ws : List (Write K V)) :
    applyAll T (w :: ws) = applyAll (w.apply T) ws := rfl

theorem applyAll_append (T : Store K V) (ws₁ ws₂ : List (Write K V)) :
    applyAll T (ws₁ ++ ws₂) = applyAll (applyAll T ws₁) ws₂ := by
  simp [applyAll, List.foldl_append]

@[simp] theorem touches_nil (k : K) : touches ([] : List (Write K V)) k = false := rfl

@[simp] theorem touches_cons (w : Write K V) (ws : List (Write K V)) (k : K) :
    touches (w :: ws) k = (w.affects k || touches ws k) := by
  simp [touches]

theorem touches_append (ws₁ ws₂ : List (Write K V)) (k : K) :
    touches (ws₁ ++ ws₂) k = (touches ws₁ k || touches ws₂ k) := by
  simp [touches, List.any_append]

theorem touches_iff {ws : List (Write K V)} {k : K} :
    touches ws k = true ↔ ∃ w ∈ ws, w.affects k = true := by
  simp [touches]

theorem touches_of_mem {ws : List (Write K V)} {w : Write K V} {k : K}
    (hw : w ∈ ws) (hk : w.affects k = true) : touches ws k = true :=
  touches_iff.2 ⟨w, hw, hk⟩

/-- A sequence leaves the keys it does not touch unchanged. -/
theorem applyAll_of_not_touches {T : Store K V} {ws : List (Write K V)} {k : K}
    (h : touches ws k = false) : applyAll T ws k = T k := by
  induction ws generalizing T with
  | nil => rfl
  | cons w ws ih =>
    simp only [touches_cons, Bool.or_eq_false_iff] at h
    rw [applyAll_cons, ih h.2, Write.apply_of_not_affects T h.1]

/-- The result of a sequence of operations depends on the starting store only
at keys the sequence does not touch. -/
theorem applyAll_congr {T₁ T₂ : Store K V} {ws : List (Write K V)}
    (h : ∀ k, touches ws k = false → T₁ k = T₂ k) :
    applyAll T₁ ws = applyAll T₂ ws := by
  induction ws generalizing T₁ T₂ with
  | nil => funext k; exact h k rfl
  | cons w ws ih =>
    rw [applyAll_cons, applyAll_cons]
    apply ih
    intro k hk
    cases hw : w.affects k with
    | true => rw [Write.apply_of_affects T₁ hw, Write.apply_of_affects T₂ hw]
    | false =>
      rw [Write.apply_of_not_affects T₁ hw, Write.apply_of_not_affects T₂ hw]
      exact h k (by simp [hw, hk])

/-- Last writer wins: applying `ws` after operations that touch only keys `ws`
also touches gives the same store as applying `ws` alone. -/
theorem applyAll_absorb {T : Store K V} {pre ws : List (Write K V)}
    (h : ∀ k, touches pre k = true → touches ws k = true) :
    applyAll (applyAll T pre) ws = applyAll T ws := by
  apply applyAll_congr
  intro k hk
  apply applyAll_of_not_touches
  cases hp : touches pre k with
  | false => rfl
  | true => simp [h k hp] at hk

theorem touches_of_touches_take {ws : List (Write K V)} {n : Nat} {k : K}
    (h : touches (ws.take n) k = true) : touches ws k = true := by
  obtain ⟨w, hw, hk⟩ := touches_iff.1 h
  exact touches_of_mem (List.mem_of_mem_take hw) hk

/-- Re-applying all of `ws` after an attempt that applied a prefix of `ws`
gives the uninterrupted result. -/
theorem applyAll_take_absorb (T : Store K V) (ws : List (Write K V)) (n : Nat) :
    applyAll (applyAll T (ws.take n)) ws = applyAll T ws :=
  applyAll_absorb fun _ => touches_of_touches_take

end Pathlang
