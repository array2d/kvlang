import Pathlang.Prog

/-!
# Crash states and replay

A process can stop between any two store operations of a step. Each
single-key operation is atomic, so a stop leaves the step's starting store with
a prefix of the step's operations applied. A `delWhere` (subtree delete) is not
atomic: a stop in the middle of it leaves any subset of its keys deleted.

`replay` proves the WAR-free hypothesis of Theorem 4 (`thm:crash`) instead of assuming
it. If a step is free of write-after-read dependences at the cut where it
stopped (it reads no key, before writing that key itself, among the keys the
interrupted attempt may have changed), then re-executing the step from the
crash state performs the same actions and ends in the same store as the
uninterrupted step.

A language step reads its counter before writing anything, so a cut after the
counter write is never write-after-read free. Those stops are not replays; the
resumption proof handles them by comparing the crash state with the store after
the step.
-/

namespace Pathlang

variable {K V I E α : Type}

/-- Keys a stop can leave partly done while the action is in progress: those
of a `delWhere`. Every other action is atomic or writes nothing. -/
def Act.partialKeys : Act K V I E → K → Bool
  | .read _ _, _ => false
  | .write (.put _ _), _ => false
  | .write (.del _), _ => false
  | .write (.delWhere P), k => P k
  | .input _, _ => false
  | .emit _, _ => false

/-- Keys partly done by the action at the head of the list, if any. -/
def headPartialKeys : List (Act K V I E) → K → Bool
  | [], _ => false
  | a :: _, k => a.partialKeys k

/-- `Act.reads as k` holds iff some action in `as` reads `k`. -/
def Act.reads : List (Act K V I E) → K → Prop
  | as, k => ∃ v, Act.read k v ∈ as

variable [DecidableEq K]

/-- `CrashAt T as j T'`: a step that started from `T` and performs the actions
`as` stops after its first `j` actions, possibly in the middle of the next one,
and leaves `T'`. -/
def CrashAt (T : Store K V) (as : List (Act K V I E)) (j : Nat) (T' : Store K V) : Prop :=
  ∀ k, T' k = applyAll T (Act.writes (as.take j)) k ∨
       (headPartialKeys (as.drop j) k = true ∧ T' k = none)

/-- Keys at which a stop at cut `j` may leave a value different from the
step's starting store. -/
def cutKeys (as : List (Act K V I E)) (j : Nat) (k : K) : Bool :=
  touches (Act.writes (as.take j)) k || headPartialKeys (as.drop j) k

/-- The step with actions `as` is free of write-after-read dependences at cut
`j`: it reads no key, before writing that key itself, among the keys a stop at
cut `j` may have changed. -/
def WarFreeAt (as : List (Act K V I E)) (j : Nat) : Prop :=
  ∀ k, Act.exposed as k = true → cutKeys as j k = false

/-- Stopping exactly between two operations is a crash state. -/
theorem crashAt_prefix (T : Store K V) (as : List (Act K V I E)) (j : Nat) :
    CrashAt T as j (applyAll T (Act.writes (as.take j))) :=
  fun _ => Or.inl rfl

/-- A crash state agrees with the starting store outside the cut's keys. -/
theorem CrashAt.agree {T T' : Store K V} {as : List (Act K V I E)} {j : Nat} {k : K}
    (hc : CrashAt T as j T') (hk : cutKeys as j k = false) : T' k = T k := by
  simp only [cutKeys, Bool.or_eq_false_iff] at hk
  rcases hc k with h | ⟨h, _⟩
  · rw [h, applyAll_of_not_touches hk.1]
  · simp [hk.2] at h

theorem touches_of_headPartialKeys {as : List (Act K V I E)} {k : K}
    (h : headPartialKeys as k = true) : touches (Act.writes as) k = true := by
  cases as with
  | nil => simp [headPartialKeys] at h
  | cons a as =>
    cases a with
    | write w =>
      cases w with
      | delWhere P =>
        simp only [headPartialKeys, Act.partialKeys] at h
        simp [Write.affects, h]
      | put _ _ => simp [headPartialKeys, Act.partialKeys] at h
      | del _ => simp [headPartialKeys, Act.partialKeys] at h
    | read _ _ => simp [headPartialKeys, Act.partialKeys] at h
    | input _ => simp [headPartialKeys, Act.partialKeys] at h
    | emit _ => simp [headPartialKeys, Act.partialKeys] at h

theorem touches_writes_of_take {as : List (Act K V I E)} {n : Nat} {k : K}
    (h : touches (Act.writes (as.take n)) k = true) : touches (Act.writes as) k = true := by
  rw [← List.take_append_drop n as, Act.writes_append, touches_append]
  simp [h]

/-- Every key a stop may have changed is one the step writes. -/
theorem touches_of_cutKeys {as : List (Act K V I E)} {j : Nat} {k : K}
    (h : cutKeys as j k = true) : touches (Act.writes as) k = true := by
  simp only [cutKeys, Bool.or_eq_true] at h
  rcases h with h | h
  · exact touches_writes_of_take h
  · rw [← List.take_append_drop j as, Act.writes_append, touches_append]
    simp [touches_of_headPartialKeys h]

theorem touches_take_of_headPartialKeys_drop :
    ∀ {as : List (Act K V I E)} {i j : Nat} {k : K},
      headPartialKeys (as.drop i) k = true → i < j →
      touches (Act.writes (as.take j)) k = true
  | [], _, _, _, h, _ => by simp [headPartialKeys] at h
  | _ :: _, _, 0, _, _, hij => absurd hij (Nat.not_lt_zero _)
  | a :: as, 0, j + 1, k, h, _ => by
      have h' : headPartialKeys (a :: as.take j) k = true := by
        simpa [headPartialKeys] using h
      simpa using touches_of_headPartialKeys h'
  | a :: as, i + 1, j + 1, k, h, hij => by
      have ih := touches_take_of_headPartialKeys_drop (as := as) (i := i) (j := j)
        (by simpa using h) (by omega)
      cases a <;> simp [ih]

/-- The keys a stop may have changed only grow as the cut moves later. -/
theorem cutKeys_mono {as : List (Act K V I E)} {i j : Nat} {k : K} (hij : i ≤ j)
    (h : cutKeys as i k = true) : cutKeys as j k = true := by
  simp only [cutKeys, Bool.or_eq_true] at h ⊢
  rcases h with h | h
  · left
    have hti : as.take i = (as.take j).take i := by
      rw [List.take_take, Nat.min_eq_left hij]
    rw [hti] at h
    exact touches_writes_of_take h
  · rcases Nat.lt_or_eq_of_le hij with hlt | rfl
    · exact Or.inl (touches_take_of_headPartialKeys_drop h hlt)
    · exact Or.inr h

/-- Write-after-read freedom at a cut implies it at every earlier cut. -/
theorem WarFreeAt.mono {as : List (Act K V I E)} {i j : Nat}
    (h : WarFreeAt as j) (hij : i ≤ j) : WarFreeAt as i := by
  intro k hk
  cases hc : cutKeys as i k with
  | false => rfl
  | true =>
    have hj := h k hk
    rw [cutKeys_mono hij hc] at hj
    simp at hj

theorem Act.reads_of_exposed : ∀ {as : List (Act K V I E)} {k : K},
    Act.exposed as k = true → Act.reads as k
  | [], _, h => by simp at h
  | .read k' v :: as, k, h => by
      simp only [Act.exposed_read, Bool.or_eq_true, decide_eq_true_eq] at h
      rcases h with rfl | h
      · exact ⟨v, List.mem_cons_self⟩
      · obtain ⟨v', hv'⟩ := Act.reads_of_exposed h
        exact ⟨v', List.mem_cons_of_mem _ hv'⟩
  | .write w :: as, k, h => by
      simp only [Act.exposed_write, Bool.and_eq_true] at h
      obtain ⟨v', hv'⟩ := Act.reads_of_exposed h.2
      exact ⟨v', List.mem_cons_of_mem _ hv'⟩
  | .input _ :: as, k, h => by
      obtain ⟨v', hv'⟩ := Act.reads_of_exposed (as := as) (by simpa using h)
      exact ⟨v', List.mem_cons_of_mem _ hv'⟩
  | .emit _ :: as, k, h => by
      obtain ⟨v', hv'⟩ := Act.reads_of_exposed (as := as) (by simpa using h)
      exact ⟨v', List.mem_cons_of_mem _ hv'⟩

/-- The paper's sufficient condition: a step that reads nothing it may have
written by cut `j` is write-after-read free at `j`. -/
theorem warFreeAt_of_reads {as : List (Act K V I E)} {j : Nat}
    (h : ∀ k, Act.reads as k → cutKeys as j k = false) : WarFreeAt as j :=
  fun k hk => h k (Act.reads_of_exposed hk)

/-- A step that writes nothing is write-after-read free at every cut. -/
theorem warFreeAt_of_writes_nil {as : List (Act K V I E)} (h : Act.writes as = [])
    (j : Nat) : WarFreeAt as j := by
  intro k _
  cases hc : cutKeys as j k with
  | false => rfl
  | true =>
    have ht := touches_of_cutKeys hc
    simp [h] at ht

/-- **A0, derived.** If a step that started from `T` stops at a cut where it
is write-after-read free, re-executing it from the crash state `T'` performs
the same actions (so it receives and emits the same inputs and events) and
ends in the same store as the uninterrupted step. -/
theorem replay {P : Prog K V I E α} {ι : Nat → I} {T T' : Store K V} {j : Nat}
    (hc : CrashAt T (exec P ι T).2 j T') (hw : WarFreeAt (exec P ι T).2 j) :
    exec P ι T' = exec P ι T ∧ after P ι T' = after P ι T := by
  have hD : ∀ k, ¬ (cutKeys (exec P ι T).2 j k = true) → T' k = T k :=
    fun k hk => hc.agree (by simpa using hk)
  have hx : ∀ k, Act.exposed (exec P ι T).2 k = true →
      ¬ (cutKeys (exec P ι T).2 j k = true) :=
    fun k hk => by simp [hw k hk]
  exact ⟨exec_congr P ι T T' _ hD hx,
         after_congr P ι T T' _ hD hx fun _ => touches_of_cutKeys⟩

end Pathlang
