import Pathlang.Invariants

/-!
# Scoped Crash-Recovery Theorem and Concrete Rule Proofs (T6)

This module builds on the base crash and replay layer (`Pathlang.Crash`) to
establish:

1. **Exact write-after-read freedom conditions and Scoped Crash-Recovery**
   (`StepWarFree`, `stepWarFree_iff_forall_cuts`, `scoped_crash_recovery`,
   `scoped_crash_recovery_write_prefix`):
   - At a specific crash cut `j`, the sharp condition is `WarFreeAt as j`: no
     *exposed* read of the step (a read occurring before the step itself writes
     that path) hits a path that a crash at cut `j` may have modified (either by
     a completed write in `as.take j` or by an interrupted `delWhere` at `j`).
   - Across an entire step program `P` (without counter conflict, such as
     `bindChild` or root `Return`), `StepWarFree P ι T` (`∀ k, Act.exposed as k =
     true → touches (Act.writes as) k = false`) is necessary and sufficient for
     `WarFreeAt as j` to hold at *every* cut `j`. Note that `P` *may* read back
     a path it wrote earlier in the same step; what is forbidden is reading a
     path *before* writing it when the step also modifies that path. The paper's
     stronger disjointness condition (`NoReadWriteOverlap`) implies `StepWarFree`.
   - Whenever `WarFreeAt` holds at a crash cut `j` (or for *any* prefix
     `(Act.writes as).take m` of the step's writes), replaying `P` from the crash
     store `T'` performs the exact same actions (`exec P ι T' = exec P ι T`),
     has the exact store effect of applying the remaining unexecuted suffix of
     writes to `T'` (`after P ι T' = applyAll T' (Act.writes (as.drop j))`), and
     reaches the identical post-step store (`after P ι T' = after P ι T`).

2. **Commit-Split Dichotomy for Language Steps (`commitSplit_crash_dichotomy`)**:
   Every counter-advancing language step reads `kappa` near its start and commits
   `put(kappa, pi')` at some action index `j_commit = pre.length`. When all
   pre-commit writes avoid exposed reads and all post-commit writes touch only
   keys ignored by `StoreAgree` at `pi'.depth`:
   - every crash at `j ≤ j_commit` (including mid-`delWhere` cuts and every
     prefix of pre-commit writes) leaves `T'(kappa) = T(kappa)` and replays `P`
     cleanly to `after P ι T` performing the exact remaining effect;
   - every crash at `j > j_commit` (including mid-`delWhere` cuts after `kappa`
     has moved) leaves `T'(kappa) = some (.pc pi')` and `StoreAgree vid T'
     (after P ι T)`, so execution continues from `pi'` without replaying the
     already-committed step.

3. **Concrete Rule Proofs and Reversed-Order Counterexamples**:
   - `Goto` (`goto_gatedStep_crash_dichotomy`) and `Br`
     (`br_gatedStep_crash_dichotomy`).
   - `Copy` without self-alias (`copy_rule_write_prefix_recovery`,
     `copy_gatedStep_crash_dichotomy`), pointer-chain self-alias counterexample
     (`copy_ptr_self_alias_fails_replay`), and staged `Copy` via a temporary slot
     (`copy_staged_via_temp_recovery`).
   - Crash-ordered `Call` and `UserCall` writing child frame `[d+1]` before
     `kappa` (`bindChild_stepWarFree`, `call_gatedStep_crash_dichotomy`,
     `userCall_gatedStep_crash_dichotomy`), plus reversed-order counterexample
     (`reversed_call_crash_counterexample`).
   - Crash-ordered `Return` moving `kappa` before `delWhere` on frame `[d]`
     (`return_gatedStep_crash_dichotomy`, `return_mid_delWhere_next_step`,
     `returnRoot_stepWarFree`), plus reversed-order counterexample
     (`reversed_return_crash_counterexample`).
-/

namespace Pathlang

/-! ## General Scoped Crash-Recovery Theorems -/

section GeneralScopedCrash

variable {K V I E α : Type} [DecidableEq K]

/-- For *any* crash state `T'` at cut `j` (including a stop in the middle of an
in-progress `delWhere`, even without `WarFreeAt`), applying the remaining suffix
of writes `Act.writes (as.drop j)` to `T'` yields the full uninterrupted write
result `applyAll T (Act.writes as)`. -/
theorem crashAt_applyAll_remaining {T T' : Store K V}
    {as : List (Act K V I E)} {j : Nat}
    (hc : CrashAt T as j T') :
    applyAll T' (Act.writes (as.drop j)) = applyAll T (Act.writes as) := by
  have hsplit : Act.writes as = Act.writes (as.take j) ++ Act.writes (as.drop j) := by
    rw [← Act.writes_append, List.take_append_drop]
  rw [hsplit, applyAll_append]
  apply applyAll_congr
  intro k hk
  have hnp : headPartialKeys (as.drop j) k = false := by
    cases hp : headPartialKeys (as.drop j) k with
    | false => rfl
    | true =>
      rw [touches_of_headPartialKeys hp] at hk
      cases hk
  rcases hc k with hEq | ⟨hp, _⟩
  · exact hEq
  · rw [hnp] at hp; cases hp

omit [DecidableEq K] in
/-- Every prefix `(Act.writes as).take m` of a step's write list corresponds to
the completed writes `Act.writes (as.take j)` at an action-level cut `j`, bounded
by `j_bound` whenever `m ≤ (Act.writes (as.take j_bound)).length`. -/
theorem writes_take_exists_cut :
    ∀ (as : List (Act K V I E)) (m j_bound : Nat),
      m ≤ (Act.writes (as.take j_bound)).length →
      ∃ j, j ≤ j_bound ∧ j ≤ as.length ∧
        Act.writes (as.take j) = (Act.writes as).take m ∧
        Act.writes (as.drop j) = (Act.writes as).drop m
  | _, 0, _, _ =>
      ⟨0, Nat.zero_le _, Nat.zero_le _, rfl, rfl⟩
  | [], m + 1, _, hm => by
      simp at hm
  | _ :: _, m + 1, 0, hm => by
      simp at hm
  | a :: as, m + 1, jb + 1, hm => by
      cases a with
      | read k v =>
        simp only [List.take_succ_cons, Act.writes_read] at hm
        obtain ⟨j, hjb, hjl, htk, hdr⟩ := writes_take_exists_cut as (m + 1) jb hm
        refine ⟨j + 1, Nat.succ_le_succ hjb, Nat.succ_le_succ hjl, ?_, ?_⟩
        · simpa using htk
        · simpa using hdr
      | write w =>
        simp only [List.take_succ_cons, Act.writes_write, List.length_cons] at hm
        have hm' : m ≤ (Act.writes (as.take jb)).length := by omega
        obtain ⟨j, hjb, hjl, htk, hdr⟩ := writes_take_exists_cut as m jb hm'
        refine ⟨j + 1, Nat.succ_le_succ hjb, Nat.succ_le_succ hjl, ?_, ?_⟩
        · simp [htk]
        · simp [hdr]
      | input i =>
        simp only [List.take_succ_cons, Act.writes_input] at hm
        obtain ⟨j, hjb, hjl, htk, hdr⟩ := writes_take_exists_cut as (m + 1) jb hm
        refine ⟨j + 1, Nat.succ_le_succ hjb, Nat.succ_le_succ hjl, ?_, ?_⟩
        · simpa using htk
        · simpa using hdr
      | emit e =>
        simp only [List.take_succ_cons, Act.writes_emit] at hm
        obtain ⟨j, hjb, hjl, htk, hdr⟩ := writes_take_exists_cut as (m + 1) jb hm
        refine ⟨j + 1, Nat.succ_le_succ hjb, Nat.succ_le_succ hjl, ?_, ?_⟩
        · simpa using htk
        · simpa using hdr

omit [DecidableEq K] in
/-- Unbounded corollary: every write-list prefix `(Act.writes as).take m` arises
at some action cut `j ≤ as.length`. -/
theorem writes_take_exists_cut_any (as : List (Act K V I E)) (m : Nat) :
    ∃ j, j ≤ as.length ∧
      Act.writes (as.take j) = (Act.writes as).take m ∧
      Act.writes (as.drop j) = (Act.writes as).drop m := by
  by_cases hm : m ≤ (Act.writes as).length
  · have hm' : m ≤ (Act.writes (as.take as.length)).length := by
      simpa [List.take_length] using hm
    obtain ⟨j, hjb, _, htk, hdr⟩ := writes_take_exists_cut as m as.length hm'
    exact ⟨j, hjb, htk, hdr⟩
  · refine ⟨as.length, Nat.le_refl _, ?_, ?_⟩
    · rw [List.take_length, List.take_of_length_le (by omega)]
    · rw [List.drop_length, List.drop_of_length_le (by omega)]
      rfl

/-- **Exact whole-step write-after-read freedom (`StepWarFree`)**:
no exposed read of `P` (a read of `k` before `P` itself writes `k`) hits any key
touched by `P`'s writes. Notice `P` *is* permitted to read back a key it wrote
earlier in the same step. -/
def StepWarFree (P : Prog K V I E α) (ι : Nat → I) (T : Store K V) : Prop :=
  ∀ k, Act.exposed (exec P ι T).2 k = true →
    touches (Act.writes (exec P ι T).2) k = false

/-- Stronger disjointness condition stated in prose (§4, Theorem 4):
`P` never reads any key that `P` writes anywhere in the step. -/
def NoReadWriteOverlap (P : Prog K V I E α) (ι : Nat → I) (T : Store K V) : Prop :=
  ∀ k, Act.reads (exec P ι T).2 k →
    touches (Act.writes (exec P ι T).2) k = false

/-- `StepWarFree` is *equivalent* (`↔`) to `WarFreeAt` holding at every cut `j`. -/
theorem stepWarFree_iff_forall_cuts {P : Prog K V I E α} {ι : Nat → I} {T : Store K V} :
    StepWarFree P ι T ↔ ∀ j, WarFreeAt (exec P ι T).2 j := by
  constructor
  · intro h j k hexp
    cases hc : cutKeys (exec P ι T).2 j k with
    | false => rfl
    | true =>
      have ht := touches_of_cutKeys hc
      rw [h k hexp] at ht
      cases ht
  · intro h k hexp
    have hj := h (exec P ι T).2.length k hexp
    simpa [cutKeys, List.take_length, List.drop_length, headPartialKeys] using hj

/-- Disjointness of read and write footprints implies `StepWarFree`. -/
theorem StepWarFree.of_noReadWriteOverlap {P : Prog K V I E α}
    {ι : Nat → I} {T : Store K V}
    (h : NoReadWriteOverlap P ι T) : StepWarFree P ι T :=
  fun k hexp => h k (Act.reads_of_exposed hexp)

/-- **Scoped Crash-Recovery Theorem (action-cut & mid-`delWhere` form)**:
if a step program `P` stops at a cut `j` where `WarFreeAt (exec P ι T).2 j`
holds (including in the middle of a `delWhere`), replaying `P` from the crash
store `T'`:
1. performs the exact same actions, consuming the same inputs and emitting the
   same events (`exec P ι T' = exec P ι T`);
2. has the exact store effect of applying the remaining suffix of writes
   `Act.writes ((exec P ι T).2.drop j)` to `T'`; and
3. reaches the identical post-step store `after P ι T`. -/
theorem scoped_crash_recovery {P : Prog K V I E α} {ι : Nat → I}
    {T T' : Store K V} {j : Nat}
    (hc : CrashAt T (exec P ι T).2 j T')
    (hw : WarFreeAt (exec P ι T).2 j) :
    exec P ι T' = exec P ι T ∧
    after P ι T' = applyAll T' (Act.writes ((exec P ι T).2.drop j)) ∧
    after P ι T' = after P ι T := by
  obtain ⟨hex, haf⟩ := replay hc hw
  refine ⟨hex, ?_, haf⟩
  rw [haf, crashAt_applyAll_remaining hc]
  rfl

/-- **Scoped Crash-Recovery Theorem (arbitrary write-prefix form)**:
if `StepWarFree P ι T` holds, then for *every* `m : Nat` and crash store
`T' = applyAll T ((Act.writes (exec P ι T).2).take m)` left by a prefix of `P`'s
writes, replaying `P` from `T'` performs the exact remaining write suffix
`((Act.writes (exec P ι T).2).drop m)` and reaches the identical post-step store
`after P ι T`. -/
theorem scoped_crash_recovery_write_prefix {P : Prog K V I E α}
    {ι : Nat → I} {T : Store K V}
    (hw : StepWarFree P ι T) (m : Nat) :
    let ws := Act.writes (exec P ι T).2
    let T' := applyAll T (ws.take m)
    exec P ι T' = exec P ι T ∧
    after P ι T' = applyAll T' (ws.drop m) ∧
    after P ι T' = after P ι T := by
  dsimp only []
  obtain ⟨j, _, htk, hdr⟩ := writes_take_exists_cut_any (exec P ι T).2 m
  have hc : CrashAt T (exec P ι T).2 j
      (applyAll T ((Act.writes (exec P ι T).2).take m)) := by
    rw [← htk]
    exact crashAt_prefix T (exec P ι T).2 j
  have hwj : WarFreeAt (exec P ι T).2 j := (stepWarFree_iff_forall_cuts.1 hw) j
  obtain ⟨hex, hrem, haf⟩ := scoped_crash_recovery hc hwj
  rw [hdr] at hrem
  exact ⟨hex, hrem, haf⟩

end GeneralScopedCrash

/-! ## Commit-Split Crash Dichotomy for Counter-Advancing Steps -/

section CommitSplit

variable {D I E α : Type}

theorem touches_writes_of_drop {K V I E : Type} [DecidableEq K]
    {as : List (Act K V I E)} {n : Nat} {k : K}
    (h : touches (Act.writes (as.drop n)) k = true) :
    touches (Act.writes as) k = true := by
  rw [← List.take_append_drop n as, Act.writes_append, touches_append]
  simp [h]

/-- At the exact commit cut `j = pre.length` before an atomic `put`, `cutKeys`
reduces to `touches (Act.writes pre)`. -/
theorem cutKeys_commitSplit {vid : Vid} {pi' : PC}
    (pre post : List (Act Key (Val D) I E)) (k : Key) :
    cutKeys (pre ++ [.write (.put (.pcKey vid) (.pc pi'))] ++ post) pre.length k =
      touches (Act.writes pre) k := by
  have hsplit : pre ++ [.write (.put (.pcKey vid) (.pc pi'))] ++ post =
      pre ++ (.write (.put (.pcKey vid) (.pc pi')) :: post) := by
    simp [List.append_assoc]
  rw [hsplit]
  simp [cutKeys, headPartialKeys, Act.partialKeys]

/-- Pre-commit write-after-read freedom: if no exposed read of `P` hits a key
written in `pre`, then `WarFreeAt (exec P ι T).2 j` holds at *every* pre-commit
cut `j ≤ pre.length`. -/
theorem commitSplit_warFree_precommit {vid : Vid} {P : Prog Key (Val D) I E α}
    {ι : Nat → I} {T : Store Key (Val D)} {pi' : PC}
    {pre post : List (Act Key (Val D) I E)}
    (hSplit : (exec P ι T).2 = pre ++ [.write (.put (.pcKey vid) (.pc pi'))] ++ post)
    (hPre : ∀ k, Act.exposed (exec P ι T).2 k = true → touches (Act.writes pre) k = false)
    {j : Nat} (hj : j ≤ pre.length) :
    WarFreeAt (exec P ι T).2 j := by
  apply WarFreeAt.mono _ hj
  intro k hexp
  have h1 := hPre k hexp
  rw [hSplit, cutKeys_commitSplit]
  exact h1

/-- Pre-commit write-prefix recovery: for *every* prefix `(Act.writes pre).take m`
of a step's pre-commit writes (`m ≤ (Act.writes pre).length`), replaying `P`
from `T' = applyAll T ((Act.writes pre).take m)` performs the exact remaining
writes `(Act.writes (exec P ι T).2).drop m` and reaches `after P ι T`. -/
theorem commitSplit_precommit_write_prefix_recovery {vid : Vid}
    {P : Prog Key (Val D) I E α} {ι : Nat → I} {T : Store Key (Val D)} {pi' : PC}
    {pre post : List (Act Key (Val D) I E)}
    (hSplit : (exec P ι T).2 = pre ++ [.write (.put (.pcKey vid) (.pc pi'))] ++ post)
    (hPre : ∀ k, Act.exposed (exec P ι T).2 k = true → touches (Act.writes pre) k = false)
    {m : Nat} (hm : m ≤ (Act.writes pre).length) :
    let ws := Act.writes (exec P ι T).2
    let T' := applyAll T ((Act.writes pre).take m)
    exec P ι T' = exec P ι T ∧
    after P ι T' = applyAll T' (ws.drop m) ∧
    after P ι T' = after P ι T := by
  dsimp only []
  have hTakePre : (exec P ι T).2.take pre.length = pre := by
    rw [hSplit]
    have hassoc : pre ++ [.write (.put (.pcKey vid) (.pc pi'))] ++ post =
        pre ++ (.write (.put (.pcKey vid) (.pc pi')) :: post) := by
      simp [List.append_assoc]
    rw [hassoc, List.take_left]
  have hm' : m ≤ (Act.writes ((exec P ι T).2.take pre.length)).length := by
    simpa [hTakePre] using hm
  obtain ⟨j, hjb, _, htk, hdr⟩ := writes_take_exists_cut (exec P ι T).2 m pre.length hm'
  have hPrefixEq : (Act.writes pre).take m = (Act.writes (exec P ι T).2).take m := by
    rw [hSplit, List.append_assoc, Act.writes_append, List.take_append_of_le_length hm]
  have hc : CrashAt T (exec P ι T).2 j (applyAll T ((Act.writes pre).take m)) := by
    rw [hPrefixEq, ← htk]
    exact crashAt_prefix T (exec P ι T).2 j
  have hwj := commitSplit_warFree_precommit hSplit hPre hjb
  obtain ⟨hex, hrem, haf⟩ := scoped_crash_recovery hc hwj
  rw [hdr] at hrem
  exact ⟨hex, hrem, haf⟩

/-- **Commit-Split Step Crash Dichotomy (`commitSplit_crash_dichotomy`)**:
suppose `(exec P ι T).2 = pre ++ [.write (.put (.pcKey vid) (.pc pi'))] ++ post`
where:
- `hPre`: no exposed read of `P` hits any key written by `pre`;
- `hPcExp`: `.pcKey vid` is exposed in `(exec P ι T).2` (holds automatically for
  every `gatedStep` passing the gate); and
- `hPost`: every write in `post` affects only keys in `IgnoredByAgree vid pi'.depth`.

Then for **every** cut `j : Nat` and crash store `T'` with `CrashAt T (exec P ι T).2 j T'`:
- **Pre-commit (`j ≤ pre.length`)**: `WarFreeAt (exec P ι T).2 j` holds, `T'`
  still holds the pre-step counter `T (.pcKey vid)`, and replaying `P` from `T'`
  performs the exact remaining effect `Act.writes ((exec P ι T).2.drop j)` and
  reaches `after P ι T`;
- **Post-commit (`pre.length < j`)**: `T'` holds the committed counter
  `some (.pc pi')` and satisfies `StoreAgree vid T' (after P ι T)`. -/
theorem commitSplit_crash_dichotomy {vid : Vid} {P : Prog Key (Val D) I E α}
    {ι : Nat → I} {T T' : Store Key (Val D)} {pi' : PC}
    {pre post : List (Act Key (Val D) I E)}
    (hSplit : (exec P ι T).2 = pre ++ [.write (.put (.pcKey vid) (.pc pi'))] ++ post)
    (hPre : ∀ k, Act.exposed (exec P ι T).2 k = true → touches (Act.writes pre) k = false)
    (hPcExp : Act.exposed (exec P ι T).2 (.pcKey vid) = true)
    (hPost : ∀ w ∈ Act.writes post, ∀ k, w.affects k = true → IgnoredByAgree vid pi'.depth k)
    {j : Nat} (hc : CrashAt T (exec P ι T).2 j T') :
    (j ≤ pre.length ∧
      WarFreeAt (exec P ι T).2 j ∧
      T' (.pcKey vid) = T (.pcKey vid) ∧
      exec P ι T' = exec P ι T ∧
      after P ι T' = applyAll T' (Act.writes ((exec P ι T).2.drop j)) ∧
      after P ι T' = after P ι T) ∨
    (pre.length < j ∧
      T' (.pcKey vid) = some (.pc pi') ∧
      StoreAgree vid T' (after P ι T)) := by
  by_cases hj : j ≤ pre.length
  · left
    have hwj := commitSplit_warFree_precommit hSplit hPre hj
    have hCutPc : cutKeys (exec P ι T).2 j (.pcKey vid) = false := hwj (.pcKey vid) hPcExp
    have hT'Pc : T' (.pcKey vid) = T (.pcKey vid) := hc.agree hCutPc
    obtain ⟨hex, hrem, haf⟩ := scoped_crash_recovery hc hwj
    exact ⟨hj, hwj, hT'Pc, hex, hrem, haf⟩
  · right
    have hlt : pre.length < j := by omega
    refine ⟨hlt, ?_⟩
    let m := j - (pre.length + 1)
    let pre1 : List (Act Key (Val D) I E) := pre ++ [.write (.put (.pcKey vid) (.pc pi'))]
    have hSplit1 : (exec P ι T).2 = pre1 ++ post := hSplit
    have hjLen : j = pre1.length + m := by
      simp [pre1, m]
      omega
    have hTakeEq : (exec P ι T).2.take j =
        (pre ++ [.write (.put (.pcKey vid) (.pc pi'))]) ++ post.take m := by
      rw [hSplit1, hjLen, List.take_append,
        List.take_of_length_le (Nat.le_add_right _ _), Nat.add_sub_cancel_left]
    have hDropEq : (exec P ι T).2.drop j = post.drop m := by
      rw [hSplit1, hjLen, List.drop_append,
        List.drop_of_length_le (Nat.le_add_right _ _), Nat.add_sub_cancel_left, List.nil_append]
    let S_c := (Write.put (.pcKey vid) (.pc pi')).apply (applyAll T (Act.writes pre))
    have hAfterEq : after P ι T = applyAll S_c (Act.writes post) := by
      unfold after
      rw [hSplit, Act.writes_append, Act.writes_append]
      simp [S_c, applyAll_append]
    have hPrefixApply : applyAll T (Act.writes ((exec P ι T).2.take j)) =
        applyAll S_c (Act.writes (post.take m)) := by
      rw [hTakeEq, Act.writes_append, Act.writes_append]
      simp [S_c, applyAll_append]
    have hPostNotTouch : ∀ k, ¬ IgnoredByAgree vid pi'.depth k →
        touches (Act.writes post) k = false := by
      intro k hkNotIg
      cases ht : touches (Act.writes post) k with
      | false => rfl
      | true =>
        obtain ⟨w, hw, haff⟩ := touches_iff.1 ht
        exact absurd (hPost w hw k haff) hkNotIg
    have hBothEq : ∀ k, ¬ IgnoredByAgree vid pi'.depth k →
        T' k = S_c k ∧ after P ι T k = S_c k := by
      intro k hkNotIg
      have hntPost := hPostNotTouch k hkNotIg
      have hAfK : after P ι T k = S_c k := by
        rw [hAfterEq]
        exact applyAll_of_not_touches hntPost
      have hntTake : touches (Act.writes (post.take m)) k = false := by
        cases ht : touches (Act.writes (post.take m)) k with
        | false => rfl
        | true =>
          have h1 : touches (Act.writes (post.take m) ++ Act.writes (post.drop m)) k = true := by
            rw [touches_append]; simp [ht]
          rw [← Act.writes_append, List.take_append_drop] at h1
          rw [hntPost] at h1; cases h1
      have hnpDrop : headPartialKeys ((exec P ι T).2.drop j) k = false := by
        rw [hDropEq]
        cases hp : headPartialKeys (post.drop m) k with
        | false => rfl
        | true =>
          have h1 := touches_writes_of_drop (touches_of_headPartialKeys hp)
          rw [hntPost] at h1; cases h1
      rcases hc k with hEq | ⟨hp, _⟩
      · rw [hEq, hPrefixApply, applyAll_of_not_touches hntTake]
        exact ⟨rfl, hAfK⟩
      · rw [hnpDrop] at hp; cases hp
    have hPcNotIg : ¬ IgnoredByAgree vid pi'.depth (.pcKey vid) :=
      pcKey_not_ignored vid pi'.depth
    have hScPc : S_c (.pcKey vid) = some (.pc pi') := by
      simp [S_c, Write.apply, Write.affects, Write.newVal]
    obtain ⟨hT'Pc, hAfPc⟩ := hBothEq (.pcKey vid) hPcNotIg
    rw [hScPc] at hT'Pc hAfPc
    refine ⟨hT'Pc, hT'Pc.trans hAfPc.symm, ?_⟩
    intro k hkNotIg
    rw [liveDepthOf_pc hT'Pc] at hkNotIg
    obtain ⟨h1, h2⟩ := hBothEq k hkNotIg
    exact h1.trans h2.symm

end CommitSplit

/-! ## Concrete Helper Lemmas for `gatedStep`, `decodeInstr`, and `bindChild` -/

section RuleHelpers

variable {D I E : Type}

/-- Predicate recognizing `/lib` code/signature/header cells. -/
def isLibCodeKey : Key → Bool
  | .libHeader _ => true
  | .libSig _ => true
  | .libParamDef _ _ _ => true
  | .libInstr _ _ => true
  | .libOperand _ _ _ _ => true
  | _ => false

private theorem exec_bind_snd {K V I E α β : Type} [DecidableEq K]
    (P : Prog K V I E α) (f : α → Prog K V I E β) (ι : Nat → I) (T : Store K V) :
    (exec (P >>= f) ι T).2 =
      (exec P ι T).2 ++ (exec (f (exec P ι T).1) (shiftInputs P ι T) (after P ι T)).2 := by
  rw [exec_bind_op]

private theorem exec_prog_bind_snd {K V I E α β : Type} [DecidableEq K]
    (P : Prog K V I E α) (f : α → Prog K V I E β) (ι : Nat → I) (T : Store K V) :
    (exec (Prog.bind P f) ι T).2 =
      (exec P ι T).2 ++ (exec (f (exec P ι T).1) (shiftInputs P ι T) (after P ι T)).2 := by
  rw [exec_bind]

private theorem exec_prog_bind_fst {K V I E α β : Type} [DecidableEq K]
    (P : Prog K V I E α) (f : α → Prog K V I E β) (ι : Nat → I) (T : Store K V) :
    (exec (Prog.bind P f) ι T).1 =
      (exec (f (exec P ι T).1) (shiftInputs P ι T) (after P ι T)).1 := by
  rw [exec_bind]

private theorem writes_readOperandsFrom_nil (fn : SymName) (row : Nat) (dir : ParamDir) :
    ∀ (idx count : Nat) (ι : Nat → I) (T : Store Key (Val D)),
      Act.writes (exec (readOperandsFrom (D := D) (I := I) (E := E) fn row dir idx count) ι T).2 = [] := by
  intro idx count
  induction count generalizing idx with
  | zero => intro _ _; rfl
  | succ count ih =>
    intro ι T
    simp only [readOperandsFrom, Prog.bind_eq, Prog.rd_bind, exec_read, Act.writes_read]
    cases T (Key.libOperand fn row dir idx) with
    | none => rfl
    | some v =>
      rw [exec_prog_bind_snd, after_readOperandsFrom]
      simp only [Act.writes_append, ih (idx + 1) _ T, List.nil_append]
      cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn row dir (idx + 1) count) ι T).1 <;>
        rfl

private theorem writes_decodeInstr_nil (vid : Vid) (pi : PC) (ι : Nat → I)
    (T : Store Key (Val D)) :
    Act.writes (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 = [] := by
  unfold decodeInstr
  cases pi.valid with
  | false => simp
  | true =>
    simp only [Bool.not_true, Bool.false_eq_true, ite_false, Prog.bind_eq, Prog.rd_bind,
      exec_read, Act.writes_read]
    cases T (.frameLib vid pi.depth) with
    | none => rfl
    | some vLib =>
      cases vLib <;> try rfl
      rename_i fn
      simp only [exec_read, Act.writes_read]
      cases T (.libInstr fn pi.row) with
      | none => rfl
      | some vIns =>
        cases vIns <;> try rfl
        rename_i op nr nw
        rw [exec_prog_bind_snd, after_readOperandsFrom]
        simp only [Act.writes_append, writes_readOperandsFrom_nil, List.nil_append]
        cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .rd 1 nr) ι T).1 with
        | none => rfl
        | some rdOps =>
          rw [exec_prog_bind_snd, after_readOperandsFrom]
          simp only [Act.writes_append, writes_readOperandsFrom_nil, List.nil_append]
          cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .wr 1 nw) _ T).1 <;>
            rfl

private theorem readOperandsFrom_reads_lib (fn : SymName) (row : Nat) (dir : ParamDir) :
    ∀ (idx count : Nat) (ι : Nat → I) (T : Store Key (Val D)) {k : Key} {v : Option (Val D)},
      Act.read k v ∈ (exec (readOperandsFrom (D := D) (I := I) (E := E) fn row dir idx count) ι T).2 →
      isLibCodeKey k = true := by
  intro idx count
  induction count generalizing idx with
  | zero => intro _ _ _ _ hm; simp [readOperandsFrom] at hm
  | succ count ih =>
    intro ι T k v hm
    simp only [readOperandsFrom, Prog.bind_eq, Prog.rd_bind, exec_read, List.mem_cons] at hm
    rcases hm with ⟨rfl, _⟩ | hm
    · rfl
    · cases cell : T (Key.libOperand fn row dir idx) with
      | none => rw [cell] at hm; simp at hm
      | some vc =>
        rw [cell] at hm
        dsimp only [] at hm
        rw [exec_prog_bind_snd, after_readOperandsFrom] at hm
        simp only [List.mem_append] at hm
        rcases hm with hm1 | hm2
        · exact ih (idx + 1) ι T hm1
        · cases hrest : (exec (readOperandsFrom (D := D) (I := I) (E := E) fn row dir (idx + 1) count) ι T).1 <;>
            (rw [hrest] at hm2; simp at hm2)

/-- Every key read by `decodeInstr vid pi` is either `.frameLib vid pi.depth` or
a `/lib` code cell (`isLibCodeKey k = true`). -/
theorem decodeInstr_reads_kind {vid : Vid} {pi : PC} {ι : Nat → I}
    {T : Store Key (Val D)} {k : Key} {v : Option (Val D)}
    (hm : Act.read k v ∈ (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2) :
    k = .frameLib vid pi.depth ∨ isLibCodeKey k = true := by
  unfold decodeInstr at hm
  cases hpv : pi.valid with
  | false => rw [hpv] at hm; simp at hm
  | true =>
    rw [hpv] at hm
    simp only [Bool.not_true, Bool.false_eq_true, ite_false, Prog.bind_eq, Prog.rd_bind,
      exec_read, List.mem_cons] at hm
    rcases hm with ⟨rfl, _⟩ | hm
    · exact Or.inl rfl
    · cases hvLib : T (.frameLib vid pi.depth) with
      | none => rw [hvLib] at hm; simp at hm
      | some vLib =>
        rw [hvLib] at hm
        cases vLib <;> try (simp at hm; done)
        rename_i fn
        simp only [exec_read, List.mem_cons] at hm
        rcases hm with ⟨rfl, _⟩ | hm
        · exact Or.inr rfl
        · cases hvIns : T (.libInstr fn pi.row) with
          | none => rw [hvIns] at hm; simp at hm
          | some vIns =>
            rw [hvIns] at hm
            cases vIns <;> try (simp at hm; done)
            rename_i op nr nw
            dsimp only [] at hm
            rw [exec_prog_bind_snd, after_readOperandsFrom] at hm
            simp only [List.mem_append] at hm
            rcases hm with hm1 | hm2
            · exact Or.inr (readOperandsFrom_reads_lib fn pi.row .rd 1 nr ι T hm1)
            · cases hrds : (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .rd 1 nr) ι T).1 with
              | none => rw [hrds] at hm2; simp at hm2
              | some rdOps =>
                rw [hrds] at hm2
                dsimp only [] at hm2
                rw [exec_prog_bind_snd, after_readOperandsFrom] at hm2
                simp only [List.mem_append] at hm2
                rcases hm2 with hm3 | hm4
                · exact Or.inr (readOperandsFrom_reads_lib fn pi.row .wr 1 nw _ T hm3)
                · cases hwrs : (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .wr 1 nw) _ T).1 <;>
                    (rw [hwrs] at hm4; simp at hm4)

private theorem writes_map_reads_nil (ks : List Key) (T : Store Key (Val D)) :
    Act.writes (ks.map fun r => (Act.read r (T r) : Act Key (Val D) I E)) = [] := by
  induction ks with
  | nil => rfl
  | cons r ks ih => simp [ih]

private theorem writes_resolveRead_nil (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    Act.writes (exec (resolveRead (I := I) (E := E) vid pi x) ι T).2 = [] := by
  rw [exec_resolveRead]
  exact writes_map_reads_nil _ T

private theorem writes_resolveWrite_nil (vid : Vid) (pi : PC) (y : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    Act.writes (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).2 = [] := by
  rw [exec_resolveWrite]
  exact writes_map_reads_nil _ T

private theorem writes_resolveRowTarget_nil (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    Act.writes (exec (resolveRowTarget (I := I) (E := E) vid pi x) ι T).2 = [] := by
  unfold resolveRowTarget
  rw [exec_bind_snd, after_resolveRead]
  simp only [Act.writes_append, writes_resolveRead_nil, List.nil_append]
  cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => rfl
  | some p =>
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, Act.writes_read]
    cases T p with
    | none => rfl
    | some v =>
      cases v with
      | int z =>
        cases z with
        | ofNat m => cases m <;> rfl
        | negSucc _ => rfl
      | _ => rfl

private theorem after_resolveRowTarget_eq (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveRowTarget (I := I) (E := E) vid pi x) ι T = T := by
  unfold after
  rw [writes_resolveRowTarget_nil]
  rfl

private theorem writes_resolveTruth_nil (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    Act.writes (exec (resolveTruth (I := I) (E := E) vid pi x) ι T).2 = [] := by
  unfold resolveTruth
  rw [exec_bind_snd, after_resolveRead]
  simp only [Act.writes_append, writes_resolveRead_nil, List.nil_append]
  cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => rfl
  | some p =>
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, Act.writes_read]
    cases T p with
    | none => rfl
    | some v => cases v <;> rfl

private theorem after_resolveTruth_eq (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveTruth (I := I) (E := E) vid pi x) ι T = T := by
  unfold after
  rw [writes_resolveTruth_nil]
  rfl

/-- Unfold `gatedStep` action list when the gate passes (`T(rho) = none`),
`T(kappa) = some (.pc pi)`, `decodeInstr` yields `.ok dec`, and `dispatchInstr`
succeeds with `.ok (pi', evs)`. -/
theorem exec_gatedStep_stepped_actions {R : Runtime D I E} {vid : Vid} {pi pi' : PC}
    {dec : DecodedInstr D} {evs : List E} {ι : Nat → I} {T : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hdisp : (exec (dispatchInstr R vid pi dec)
      (shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T) T).1 = .ok (pi', evs)) :
    (exec (gatedStep R vid) ι T).2 =
      [.read (.stopKey vid) none, .read (.pcKey vid) (some (.pc pi))] ++
      (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 ++
      (exec (dispatchInstr R vid pi dec)
        (shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T) T).2 ∧
    Act.exposed (exec (gatedStep R vid) ι T).2 (.pcKey vid) = true := by
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hacts : (exec (gatedStep R vid) ι T).2 =
      .read (.stopKey vid) none :: .read (.pcKey vid) (some (.pc pi)) ::
      ((exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 ++
       (exec (dispatchInstr R vid pi dec)
         (shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T) T).2) := by
    unfold gatedStep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho]
    rw [exec_prog_bind_snd]
    unfold stepCore
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read]
    rw [exec_prog_bind_fst, exec_prog_bind_snd, after_bind, hdecEx, after_decodeInstr]
    dsimp only []
    rw [hdisp]
    simp [finalizeStep]
  refine ⟨?_, ?_⟩
  · simpa [List.append_assoc] using hacts
  · rw [hacts]
    simp [Act.exposed_read]

theorem not_inChild_of_not_forbidden {vid : Vid} {d : Nat} {k : Key}
    (h : ForbiddenKey vid d k = false) :
    inFrame vid (d + 1) k = false := by
  cases hin : inFrame vid (d + 1) k with
  | false => rfl
  | true =>
    have hdead : isDeadFrameKey vid d k = true := by
      cases k <;> simp_all [inFrame, isDeadFrameKey] <;> omega
    simp [ForbiddenKey, hdead] at h

private theorem resolveRead_eq_of_agree_nonchild {vid : Vid} {pi : PC} {x : Key × Val D}
    {T S : Store Key (Val D)} (ιS : Nat → I)
    (hx : ReadOpSafe vid pi T x)
    (hEq : ∀ k, inFrame vid (pi.depth + 1) k = false → S k = T k) :
    (exec (resolveRead (I := I) (E := E) vid pi x) ιS S).2 =
      (resolveReadReads vid pi x T).map (fun r => Act.read r (T r)) := by
  have hCongr : exec (resolveRead (I := I) (E := E) vid pi x) ιS S =
      exec (resolveRead (I := I) (E := E) vid pi x) ιS T := by
    apply exec_congr _ _ T S (fun k => inFrame vid (pi.depth + 1) k = true)
    · intro k hk; exact hEq k (by simpa using hk)
    · intro k hexp
      obtain ⟨v, hm⟩ := Act.reads_of_exposed hexp
      rw [exec_resolveRead] at hm
      simp only [List.mem_map] at hm
      obtain ⟨r, hr, hEqAct⟩ := hm
      cases hEqAct
      simp [not_inChild_of_not_forbidden (hx.1 k hr)]
  rw [hCongr, exec_resolveRead]

private theorem resolveWrite_eq_of_agree_nonchild {vid : Vid} {pi : PC} {y : Key × Val D}
    {T S : Store Key (Val D)} (ιS : Nat → I)
    (hy : WriteOpSafe vid pi T y)
    (hEq : ∀ k, inFrame vid (pi.depth + 1) k = false → S k = T k) :
    (exec (resolveWrite (I := I) (E := E) vid pi y) ιS S).2 =
      (resolveWriteReads vid pi y T).map (fun r => Act.read r (T r)) := by
  have hCongr : exec (resolveWrite (I := I) (E := E) vid pi y) ιS S =
      exec (resolveWrite (I := I) (E := E) vid pi y) ιS T := by
    apply exec_congr _ _ T S (fun k => inFrame vid (pi.depth + 1) k = true)
    · intro k hk; exact hEq k (by simpa using hk)
    · intro k hexp
      obtain ⟨v, hm⟩ := Act.reads_of_exposed hexp
      rw [exec_resolveWrite] at hm
      simp only [List.mem_map] at hm
      obtain ⟨r, hr, hEqAct⟩ := hm
      cases hEqAct
      simp [not_inChild_of_not_forbidden (hy.1 k hr)]
  rw [hCongr, exec_resolveWrite]

private theorem bindReadArgs_avoids_child {vid : Vid} {pi : PC} {T : Store Key (Val D)} :
    ∀ (idx : Nat) (xs : List (Key × Val D)) (ι : Nat → I) (S : Store Key (Val D)),
      (∀ x ∈ xs, ReadOpSafe vid pi T x) →
      (∀ k, inFrame vid (pi.depth + 1) k = false → S k = T k) →
      ExecAvoids (fun k => inFrame vid (pi.depth + 1) k = true)
        (fun k => inFrame vid (pi.depth + 1) k = false)
        (bindReadArgs (I := I) (E := E) vid pi idx xs) ι S := by
  intro idx xs
  induction xs generalizing idx with
  | nil => intro _ _ _ _; exact ExecAvoids.ret _ _ _
  | cons x xs ih =>
    intro ι S hxs hEq
    have hx := hxs x List.mem_cons_self
    unfold bindReadArgs
    apply ExecAvoids.bind_op
    · refine ⟨?_, ?_⟩
      · intro k v hm
        rw [resolveRead_eq_of_agree_nonchild ι hx hEq] at hm
        simp only [List.mem_map] at hm
        obtain ⟨r, hr, hAct⟩ := hm
        cases hAct
        simp [not_inChild_of_not_forbidden (hx.1 k hr)]
      · intro w hm
        have hnil := writes_resolveRead_nil (I := I) (E := E) vid pi x ι S
        have hmw := Act.mem_writes_of_mem hm
        rw [hnil] at hmw; cases hmw
    · rw [after_resolveRead]
      cases (exec (resolveRead (I := I) (E := E) vid pi x) ι S).1 with
      | none => exact ExecAvoids.ret _ _ _
      | some p =>
        apply ExecAvoids.wr_bind
        · intro k hk hfalse
          simp only [Write.affects, decide_eq_true_eq] at hk
          subst hk; simp [inFrame] at hfalse
        · apply ih (idx + 1) _ _ (fun x' hx' => hxs x' (List.mem_cons_of_mem _ hx'))
          intro k hk
          have hne : k ≠ .frameParam vid (pi.depth + 1) .rd idx := by
            rintro rfl; simp [inFrame] at hk
          rw [Write.apply_of_not_affects S (by simp [Write.affects, hne])]
          exact hEq k hk

private theorem bindWriteArgs_avoids_child {vid : Vid} {pi : PC} {T : Store Key (Val D)} :
    ∀ (idx : Nat) (ys : List (Key × Val D)) (ι : Nat → I) (S : Store Key (Val D)),
      (∀ y ∈ ys, WriteOpSafe vid pi T y) →
      (∀ k, inFrame vid (pi.depth + 1) k = false → S k = T k) →
      ExecAvoids (fun k => inFrame vid (pi.depth + 1) k = true)
        (fun k => inFrame vid (pi.depth + 1) k = false)
        (bindWriteArgs (I := I) (E := E) vid pi idx ys) ι S := by
  intro idx ys
  induction ys generalizing idx with
  | nil => intro _ _ _ _; exact ExecAvoids.ret _ _ _
  | cons y ys ih =>
    intro ι S hys hEq
    have hy := hys y List.mem_cons_self
    unfold bindWriteArgs
    apply ExecAvoids.bind_op
    · refine ⟨?_, ?_⟩
      · intro k v hm
        rw [resolveWrite_eq_of_agree_nonchild ι hy hEq] at hm
        simp only [List.mem_map] at hm
        obtain ⟨r, hr, hAct⟩ := hm
        cases hAct
        simp [not_inChild_of_not_forbidden (hy.1 k hr)]
      · intro w hm
        have hnil := writes_resolveWrite_nil (I := I) (E := E) vid pi y ι S
        have hmw := Act.mem_writes_of_mem hm
        rw [hnil] at hmw; cases hmw
    · rw [after_resolveWrite]
      cases (exec (resolveWrite (I := I) (E := E) vid pi y) ι S).1 with
      | none => exact ExecAvoids.ret _ _ _
      | some q =>
        apply ExecAvoids.wr_bind
        · intro k hk hfalse
          simp only [Write.affects, decide_eq_true_eq] at hk
          subst hk; simp [inFrame] at hfalse
        · apply ih (idx + 1) _ _ (fun y' hy' => hys y' (List.mem_cons_of_mem _ hy'))
          intro k hk
          have hne : k ≠ .frameParam vid (pi.depth + 1) .wr idx := by
            rintro rfl; simp [inFrame] at hk
          rw [Write.apply_of_not_affects S (by simp [Write.affects, hne])]
          exact hEq k hk

/-- Under operand safety, `bindChild` never reads any key in `inFrame vid (pi.depth + 1)`
and writes *only* keys in `inFrame vid (pi.depth + 1)`. -/
theorem bindChild_child_footprint {vid : Vid} {pi : PC} {callee : SymName}
    {rdArgs wrArgs : List (Key × Val D)} {ι : Nat → I} {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdArgs, ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrArgs, WriteOpSafe vid pi T y) :
    ExecAvoids (fun k => inFrame vid (pi.depth + 1) k = true)
      (fun k => inFrame vid (pi.depth + 1) k = false)
      (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T := by
  unfold bindChild
  apply ExecAvoids.rd_bind (by simp [inFrame])
  cases T (.libSig callee) with
  | none => exact ExecAvoids.ret _ _ _
  | some vSig =>
    cases vSig <;> try exact ExecAvoids.ret _ _ _
    rename_i sigNr sigNw
    cases har : (sigNr != rdArgs.length || sigNw != wrArgs.length) with
    | true =>
      simp only [har, ite_true]
      exact ExecAvoids.ret _ _ _
    | false =>
      simp only [har, Bool.false_eq_true, ite_false]
      let dChild := pi.depth + 1
      apply ExecAvoids.wr_bind
      · intro k hk hfalse; simp [Write.affects] at hk; rw [hk] at hfalse; cases hfalse
      apply ExecAvoids.wr_bind
      · intro k hk hfalse; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame] at hfalse
      apply ExecAvoids.wr_bind
      · intro k hk hfalse; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame] at hfalse
      apply ExecAvoids.wr_bind
      · intro k hk hfalse; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame] at hfalse
      have hS₁Eq : ∀ k, inFrame vid (pi.depth + 1) k = false →
          ((Write.put (.frameCallPc vid dChild) (.pc pi.entry)).apply
            ((Write.put (.frameRetPc vid dChild) (.pc pi.next)).apply
              ((Write.put (.frameLib vid dChild) (.sym callee)).apply
                ((Write.delWhere (inFrame vid dChild)).apply T)))) k = T k := by
        intro k hk
        have h1 : k ≠ .frameCallPc vid dChild := by rintro rfl; simp [dChild, inFrame] at hk
        have h2 : k ≠ .frameRetPc vid dChild := by rintro rfl; simp [dChild, inFrame] at hk
        have h3 : k ≠ .frameLib vid dChild := by rintro rfl; simp [dChild, inFrame] at hk
        simp [Write.apply, Write.affects, h1, h2, h3, dChild, hk]
      have hRdAv := bindReadArgs_avoids_child (E := E) 1 rdArgs ι _ hrd hS₁Eq
      apply ExecAvoids.bind_op hRdAv
      cases (exec (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) ι _).1 with
      | error _ => exact ExecAvoids.ret _ _ _
      | ok _ =>
        apply bindWriteArgs_avoids_child (E := E) 1 wrArgs _ _ hwr
        intro k hk
        rw [hRdAv.after_eq hk]
        exact hS₁Eq k hk

end RuleHelpers

/-! ## Concrete Rule Proofs (`Goto`, `Br`, `Copy`, `Call`, `UserCall`, `Return`) -/

section ConcreteRules

variable {D I E : Type}

/-! ### 1. `Goto` and `Br` -/

/-- **Crash recovery for `Goto`**: every `gatedStep` executing `Goto` has zero
pre-commit writes before `put(kappa, pi')` and zero post-commit writes, so every
pre-commit crash cut satisfies `WarFreeAt` and replays `Goto` cleanly, while
every post-commit cut equals `after (gatedStep R vid) ι T`. -/
theorem goto_gatedStep_crash_dichotomy {R : Runtime D I E} {vid : Vid}
    {pi pi' : PC} {dec : DecodedInstr D} {evs : List E}
    {ι : Nat → I} {T T' : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hop : dec.op = "goto")
    (hstep : (exec (gatedStep R vid) ι T).1 = .stepped pi' evs)
    {j : Nat} (hc : CrashAt T (exec (gatedStep R vid) ι T).2 j T') :
    ∃ j_commit,
      (j ≤ j_commit ∧
        WarFreeAt (exec (gatedStep R vid) ι T).2 j ∧
        T' (.pcKey vid) = T (.pcKey vid) ∧
        exec (gatedStep R vid) ι T' = exec (gatedStep R vid) ι T ∧
        after (gatedStep R vid) ι T' =
          applyAll T' (Act.writes ((exec (gatedStep R vid) ι T).2.drop j)) ∧
        after (gatedStep R vid) ι T' = after (gatedStep R vid) ι T) ∨
      (j_commit < j ∧
        T' (.pcKey vid) = some (.pc pi') ∧
        StoreAgree vid T' (after (gatedStep R vid) ι T)) := by
  let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hdisp : (exec (dispatchInstr R vid pi dec) ιd T).1 = .ok (pi', evs) := by
    unfold gatedStep at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho] at hstep
    rw [exec_prog_bind_fst] at hstep
    unfold stepCore at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read] at hstep
    rw [exec_prog_bind_fst, hdecEx, after_decodeInstr] at hstep
    dsimp only [] at hstep
    cases hd : (exec (dispatchInstr R vid pi dec) ιd T).1 with
    | error err => rw [hd] at hstep; simp [finalizeStep] at hstep
    | ok pair =>
      rcases pair with ⟨p2, e2⟩
      rw [hd] at hstep; simp [finalizeStep] at hstep
      rcases hstep with ⟨rfl, rfl⟩
      rfl
  obtain ⟨hActsG, hPcExp⟩ := exec_gatedStep_stepped_actions hrho hpc hdec hdisp
  have hDispEq : exec (dispatchInstr R vid pi dec) ιd T =
      exec (gotoRuleProg vid pi dec.rdOps dec.wrOps) ιd T := by
    unfold dispatchInstr
    simp [hop]
  rw [hDispEq] at hdisp hActsG
  unfold gotoRuleProg at hdisp hActsG
  match hrdOps : dec.rdOps, hwrOps : dec.wrOps with
  | [rd1], [] =>
    rw [hrdOps, hwrOps] at hdisp hActsG
    dsimp only [] at hdisp hActsG
    rw [exec_bind_fst, after_resolveRowTarget_eq] at hdisp
    cases hn : (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) ιd T).1 with
    | none => rw [hn] at hdisp; simp at hdisp
    | some n =>
      rw [hn] at hdisp; simp at hdisp
      rcases hdisp with ⟨rfl, rfl⟩
      rw [exec_bind_snd, hn, after_resolveRowTarget_eq] at hActsG
      dsimp only [] at hActsG
      simp only [Prog.bind_eq, Prog.wr_bind, exec_write, Prog.pure_eq, exec_ret] at hActsG
      let pre := [.read (.stopKey vid) none, .read (.pcKey vid) (some (.pc pi))] ++
        (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 ++
        (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) ιd T).2
      have hSplit : (exec (gatedStep R vid) ι T).2 =
          pre ++ [.write (.put (.pcKey vid) (.pc (pi.target n)))] ++ [] := by
        simpa [pre, List.append_assoc] using hActsG
      have hPreW : Act.writes pre = [] := by
        simp [pre, Act.writes_append, writes_decodeInstr_nil, writes_resolveRowTarget_nil]
      refine ⟨pre.length, commitSplit_crash_dichotomy hSplit ?_ hPcExp ?_ hc⟩
      · intro k _; simp [hPreW]
      · intro w hw; simp at hw
  | [], _ => rw [hrdOps] at hdisp; simp at hdisp
  | _ :: _ :: _, _ => rw [hrdOps] at hdisp; simp at hdisp
  | [_], _ :: _ => rw [hrdOps, hwrOps] at hdisp; simp at hdisp

/-- **Crash recovery for `Br`**: every `gatedStep` executing `Br` has zero
pre-commit writes before `put(kappa, pi')` and zero post-commit writes, so every
pre-commit crash cut satisfies `WarFreeAt` and replays `Br` cleanly, while every
post-commit cut equals `after (gatedStep R vid) ι T`. -/
theorem br_gatedStep_crash_dichotomy {R : Runtime D I E} {vid : Vid}
    {pi pi' : PC} {dec : DecodedInstr D} {evs : List E}
    {ι : Nat → I} {T T' : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hop : dec.op = "br")
    (hstep : (exec (gatedStep R vid) ι T).1 = .stepped pi' evs)
    {j : Nat} (hc : CrashAt T (exec (gatedStep R vid) ι T).2 j T') :
    ∃ j_commit,
      (j ≤ j_commit ∧
        WarFreeAt (exec (gatedStep R vid) ι T).2 j ∧
        T' (.pcKey vid) = T (.pcKey vid) ∧
        exec (gatedStep R vid) ι T' = exec (gatedStep R vid) ι T ∧
        after (gatedStep R vid) ι T' =
          applyAll T' (Act.writes ((exec (gatedStep R vid) ι T).2.drop j)) ∧
        after (gatedStep R vid) ι T' = after (gatedStep R vid) ι T) ∨
      (j_commit < j ∧
        T' (.pcKey vid) = some (.pc pi') ∧
        StoreAgree vid T' (after (gatedStep R vid) ι T)) := by
  let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hdisp : (exec (dispatchInstr R vid pi dec) ιd T).1 = .ok (pi', evs) := by
    unfold gatedStep at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho] at hstep
    rw [exec_prog_bind_fst] at hstep
    unfold stepCore at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read] at hstep
    rw [exec_prog_bind_fst, hdecEx, after_decodeInstr] at hstep
    dsimp only [] at hstep
    cases hd : (exec (dispatchInstr R vid pi dec) ιd T).1 with
    | error err => rw [hd] at hstep; simp [finalizeStep] at hstep
    | ok pair =>
      rcases pair with ⟨p2, e2⟩
      rw [hd] at hstep; simp [finalizeStep] at hstep
      rcases hstep with ⟨rfl, rfl⟩
      rfl
  obtain ⟨hActsG, hPcExp⟩ := exec_gatedStep_stepped_actions hrho hpc hdec hdisp
  have hDispEq : exec (dispatchInstr R vid pi dec) ιd T =
      exec (brRuleProg vid pi dec.rdOps dec.wrOps) ιd T := by
    unfold dispatchInstr
    simp [hop]
  rw [hDispEq] at hdisp hActsG
  unfold brRuleProg at hdisp hActsG
  match hrdOps : dec.rdOps, hwrOps : dec.wrOps with
  | [rd1, rd2, rd3], [] =>
    rw [hrdOps, hwrOps] at hdisp hActsG
    dsimp only [] at hdisp hActsG
    rw [exec_bind_fst, after_resolveTruth_eq] at hdisp
    cases hb : (exec (resolveTruth (I := I) (E := E) vid pi rd1) ιd T).1 with
    | none => rw [hb] at hdisp; simp at hdisp
    | some b =>
      rw [hb] at hdisp; dsimp only [] at hdisp
      let ιt := shiftInputs (resolveTruth (I := I) (E := E) vid pi rd1) ιd T
      rw [exec_bind_fst, after_resolveRowTarget_eq] at hdisp
      cases hn : (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) ιt T).1 with
      | none => rw [hn] at hdisp; simp at hdisp
      | some n =>
        rw [hn] at hdisp; simp at hdisp
        rcases hdisp with ⟨rfl, rfl⟩
        rw [exec_bind_snd, hb, after_resolveTruth_eq] at hActsG
        dsimp only [] at hActsG
        rw [exec_bind_snd, hn, after_resolveRowTarget_eq] at hActsG
        dsimp only [] at hActsG
        simp only [Prog.bind_eq, Prog.wr_bind, exec_write, Prog.pure_eq, exec_ret] at hActsG
        let pre := [.read (.stopKey vid) none, .read (.pcKey vid) (some (.pc pi))] ++
          (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 ++
          (exec (resolveTruth (I := I) (E := E) vid pi rd1) ιd T).2 ++
          (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) ιt T).2
        have hSplit : (exec (gatedStep R vid) ι T).2 =
            pre ++ [.write (.put (.pcKey vid) (.pc (pi.target n)))] ++ [] := by
          simpa [pre, ιt, List.append_assoc] using hActsG
        have hPreW : Act.writes pre = [] := by
          simp [pre, Act.writes_append, writes_decodeInstr_nil,
            writes_resolveTruth_nil, writes_resolveRowTarget_nil]
        refine ⟨pre.length, commitSplit_crash_dichotomy hSplit ?_ hPcExp ?_ hc⟩
        · intro k _; simp [hPreW]
        · intro w hw; simp at hw
  | [], _ => rw [hrdOps] at hdisp; simp at hdisp
  | [_], _ => rw [hrdOps] at hdisp; simp at hdisp
  | [_, _], _ => rw [hrdOps] at hdisp; simp at hdisp
  | _ :: _ :: _ :: _ :: _, _ => rw [hrdOps] at hdisp; simp at hdisp
  | [_, _, _], _ :: _ => rw [hrdOps, hwrOps] at hdisp; simp at hdisp

/-! ### 2. `Copy`: Non-Self-Aliasing Recovery, Aliasing Counterexample, and Staging via Temporary -/

/-- Exact non-self-aliasing condition on `Copy`: the resolved destination `q`
differs from the resolved source `p`, is not traversed as a pointer link while
resolving `rd1` or `wr1`, and is not a `/lib` code cell. -/
def CopyNonAliased (vid : Vid) (pi : PC) (T : Store Key (Val D))
    (rd1 wr1 : Key × Val D) (p q : Key) : Prop :=
  q ≠ p ∧
  q ∉ resolveReadReads vid pi rd1 T ∧
  q ∉ resolveWriteReads vid pi wr1 T ∧
  isLibCodeKey q = false

/-- Rule-level write-prefix recovery for `Copy`: since `copyRuleProg` does not
itself read `kappa`, under `ReadOpSafe` and `WriteOpSafe` together with
`q ≠ p`, `q ∉ resolveReadReads`, and `q ∉ resolveWriteReads`, `copyRuleProg`
satisfies global `StepWarFree`, so replaying `copyRuleProg` from *any* prefix of
its writes (`[]`, `[put(q, val)]`, or `[put(q, val), put(kappa, pi.next)]`)
performs the exact remaining write effect and reaches
`after (copyRuleProg ...) ι T`. -/
theorem copy_rule_write_prefix_recovery {vid : Vid} {pi : PC}
    {rd1 wr1 : Key × Val D} {p q : Key} {val : Val D}
    {ι : Nat → I} {T : Store Key (Val D)}
    (hrd1 : ReadOpSafe vid pi T rd1)
    (hwr1 : WriteOpSafe vid pi T wr1)
    (hp : resolveReadPure vid pi rd1 T = some p)
    (hq : resolveWritePure vid pi wr1 T = some q)
    (hval : T p = some val)
    (hqNeP : q ≠ p)
    (hqNotRd : q ∉ resolveReadReads vid pi rd1 T)
    (hqNotWr : q ∉ resolveWriteReads vid pi wr1 T)
    (m : Nat) :
    let P := copyRuleProg (I := I) (E := E) vid pi [rd1] [wr1]
    let ws := Act.writes (exec P ι T).2
    let T' := applyAll T (ws.take m)
    StepWarFree P ι T ∧
    exec P ι T' = exec P ι T ∧
    after P ι T' = applyAll T' (ws.drop m) ∧
    after P ι T' = after P ι T := by
  dsimp only []
  let P := copyRuleProg (I := I) (E := E) vid pi [rd1] [wr1]
  have hActs : (exec P ι T).2 =
      (resolveReadReads vid pi rd1 T).map (fun r => Act.read r (T r)) ++
      (resolveWriteReads vid pi wr1 T).map (fun r => Act.read r (T r)) ++
      [.read p (some val), .write (.put q val), .write (.put (.pcKey vid) (.pc pi.next))] := by
    dsimp [P, copyRuleProg]
    rw [exec_prog_bind_snd, exec_resolveRead, after_resolveRead, hp]
    dsimp only []
    rw [exec_prog_bind_snd, exec_resolveWrite, after_resolveWrite, hq]
    dsimp only []
    simp [hval, List.append_assoc]
  have notPcOfNotForb : ∀ {r : Key}, ForbiddenKey vid pi.depth r = false → r ≠ .pcKey vid := by
    intro r hr hEq
    subst hEq
    simp [ForbiddenKey, isThreadControlKey] at hr
  have hNoOverlap : NoReadWriteOverlap P ι T := by
    intro k ⟨v, hm⟩
    rw [hActs] at hm ⊢
    simp only [Act.writes_append, writes_map_reads_nil, List.nil_append,
      Act.writes_read, Act.writes_write, Act.writes_nil,
      touches_cons, touches_nil, Write.affects, Bool.or_false,
      Bool.or_eq_false_iff, decide_eq_false_iff_not]
    simp only [List.mem_append, List.mem_map, List.mem_cons, List.not_mem_nil, or_false] at hm
    rcases hm with (⟨r, hr, hEq⟩ | ⟨r, hr, hEq⟩) | (hEq | hEq | hEq)
    · cases hEq
      exact ⟨fun h => hqNotRd (by subst h; exact hr), notPcOfNotForb (hrd1.1 _ hr)⟩
    · cases hEq
      exact ⟨fun h => hqNotWr (by subst h; exact hr), notPcOfNotForb (hwr1.1 _ hr)⟩
    · cases hEq
      exact ⟨hqNeP.symm, notPcOfNotForb (hrd1.2 p hp)⟩
    · cases hEq
    · cases hEq
  have hSwf : StepWarFree P ι T := StepWarFree.of_noReadWriteOverlap hNoOverlap
  obtain ⟨hex, hrem, haf⟩ := scoped_crash_recovery_write_prefix hSwf m
  exact ⟨hSwf, hex, hrem, haf⟩

/-- **Crash recovery for `Copy` at `gatedStep` level**: under `StepDiscipline`
and `CopyNonAliased`, every pre-commit crash cut `j ≤ j_commit` (including the
cut *after* `put(q, val)` and *before* `put(kappa, pi.next)`) satisfies
`WarFreeAt` and replays `gatedStep` cleanly to `after (gatedStep R vid) ι T`,
while every post-commit cut agrees with `after (gatedStep R vid) ι T`. -/
theorem copy_gatedStep_crash_dichotomy {R : Runtime D I E} {vid : Vid}
    {pi pi' : PC} {dec : DecodedInstr D} {rd1 wr1 : Key × Val D}
    {p q : Key} {evs : List E} {ι : Nat → I} {T T' : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hop : dec.op = "=")
    (hrdOps : dec.rdOps = [rd1])
    (hwrOps : dec.wrOps = [wr1])
    (hP : StepDiscipline vid T)
    (hp : resolveReadPure vid pi rd1 T = some p)
    (hq : resolveWritePure vid pi wr1 T = some q)
    (hNonAlias : CopyNonAliased vid pi T rd1 wr1 p q)
    (hstep : (exec (gatedStep R vid) ι T).1 = .stepped pi' evs)
    {j : Nat} (hc : CrashAt T (exec (gatedStep R vid) ι T).2 j T') :
    ∃ j_commit,
      (j ≤ j_commit ∧
        WarFreeAt (exec (gatedStep R vid) ι T).2 j ∧
        T' (.pcKey vid) = T (.pcKey vid) ∧
        exec (gatedStep R vid) ι T' = exec (gatedStep R vid) ι T ∧
        after (gatedStep R vid) ι T' =
          applyAll T' (Act.writes ((exec (gatedStep R vid) ι T).2.drop j)) ∧
        after (gatedStep R vid) ι T' = after (gatedStep R vid) ι T) ∨
      (j_commit < j ∧
        T' (.pcKey vid) = some (.pc pi') ∧
        StoreAgree vid T' (after (gatedStep R vid) ι T)) := by
  obtain ⟨_, hwrSafe⟩ := hP pi dec hpc hdec
  have hw1 : WriteOpSafe vid pi T wr1 := hwrSafe wr1 (by simp [hwrOps])
  have hqNotForb : ForbiddenKey vid pi.depth q = false := (hw1.2 q hq).1
  let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hdisp : (exec (dispatchInstr R vid pi dec) ιd T).1 = .ok (pi', evs) := by
    unfold gatedStep at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho] at hstep
    rw [exec_prog_bind_fst] at hstep
    unfold stepCore at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read] at hstep
    rw [exec_prog_bind_fst, hdecEx, after_decodeInstr] at hstep
    dsimp only [] at hstep
    cases hd : (exec (dispatchInstr R vid pi dec) ιd T).1 with
    | error err => rw [hd] at hstep; simp [finalizeStep] at hstep
    | ok pair =>
      rcases pair with ⟨p2, e2⟩
      rw [hd] at hstep; simp [finalizeStep] at hstep
      rcases hstep with ⟨rfl, rfl⟩
      rfl
  obtain ⟨hActsG, hPcExp⟩ := exec_gatedStep_stepped_actions hrho hpc hdec hdisp
  have hDispEq : exec (dispatchInstr R vid pi dec) ιd T =
      exec (copyRuleProg vid pi [rd1] [wr1]) ιd T := by
    unfold dispatchInstr
    simp [hop, hrdOps, hwrOps]
  rw [hDispEq] at hdisp hActsG
  unfold copyRuleProg at hdisp hActsG
  dsimp only [] at hdisp hActsG
  rw [exec_bind_fst, exec_resolveRead, after_resolveRead, hp] at hdisp
  rw [exec_bind_snd, exec_resolveRead, after_resolveRead, hp] at hActsG
  dsimp only [] at hdisp hActsG
  rw [exec_bind_fst, exec_resolveWrite, after_resolveWrite, hq] at hdisp
  rw [exec_bind_snd, exec_resolveWrite, after_resolveWrite, hq] at hActsG
  dsimp only [] at hdisp hActsG
  simp only [Prog.bind_eq, Prog.rd_bind, exec_read] at hdisp hActsG
  cases hval : T p with
  | none =>
    rw [hval] at hdisp
    simp at hdisp
  | some val =>
    rw [hval] at hdisp hActsG
    simp only [Prog.wr_bind, exec_write, Prog.pure_eq, exec_ret] at hdisp hActsG
    cases hdisp
    let pre := [.read (.stopKey vid) none, .read (.pcKey vid) (some (.pc pi))] ++
      (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 ++
      (resolveReadReads vid pi rd1 T).map (fun r => Act.read r (T r)) ++
      (resolveWriteReads vid pi wr1 T).map (fun r => Act.read r (T r)) ++
      [.read p (some val), .write (.put q val)]
    have hSplit : (exec (gatedStep R vid) ι T).2 =
        pre ++ [.write (.put (.pcKey vid) (.pc pi.next))] ++ [] := by
      simpa [pre, List.append_assoc] using hActsG
    have hPreW : Act.writes pre = [.put q val] := by
      simp [pre, Act.writes_append, writes_decodeInstr_nil, writes_map_reads_nil]
    have hNotReadQ : ¬ Act.reads (exec (gatedStep R vid) ι T).2 q := by
      rintro ⟨v, hm⟩
      rw [hSplit] at hm
      simp only [pre, List.append_nil, List.mem_append, List.mem_cons,
        List.not_mem_nil, or_false, List.mem_map] at hm
      rcases hm with (((((hEq | hEq) | hmDec) | ⟨r, hr, hEq⟩) | ⟨r, hr, hEq⟩) | (hEq | hEq)) | hEq
      · cases hEq
        simp [ForbiddenKey, isThreadControlKey] at hqNotForb
      · cases hEq
        simp [ForbiddenKey, isThreadControlKey] at hqNotForb
      · rcases decodeInstr_reads_kind hmDec with hLibEq | hCode
        · subst hLibEq
          simp [ForbiddenKey, isThreadControlKey] at hqNotForb
        · rw [hNonAlias.2.2.2] at hCode
          cases hCode
      · cases hEq
        exact hNonAlias.2.1 hr
      · cases hEq
        exact hNonAlias.2.2.1 hr
      · cases hEq
        exact hNonAlias.1 rfl
      · cases hEq
      · cases hEq
    refine ⟨pre.length, commitSplit_crash_dichotomy hSplit ?_ hPcExp ?_ hc⟩
    · intro k hexp
      rw [hPreW]
      simp only [touches_cons, touches_nil, Write.affects, Bool.or_false,
        decide_eq_false_iff_not]
      rintro rfl
      exact hNotReadQ (Act.reads_of_exposed hexp)
    · intro w hw; simp at hw

/-- **Counterexample for unstaged pointer-self-aliasing `Copy(*s, s)`**:
slot `s` holds pointer `.ptr x` and `x` holds `.int 42`; `Copy(*s, s)` resolves
`*s` through `s` to `x`, writes `.int 42` into `s`, and crashes before updating
`kappa`. At that cut `WarFreeAt` fails (because `s` was read during `resolveRead`
before being overwritten), and replaying `gatedStep` from the crash store fails
with `.failed "copy_bad_src"`. -/
theorem copy_ptr_self_alias_fails_replay :
    ∃ (R : Runtime Unit Unit Unit) (vid : Vid) (T T' : Store Key (Val Unit)) (j : Nat),
      StepDiscipline vid T ∧
      T (.stopKey vid) = none ∧
      (exec (gatedStep R vid) (fun _ => ()) T).1 = .stepped ⟨1, 2⟩ [] ∧
      CrashAt T (exec (gatedStep R vid) (fun _ => ()) T).2 j T' ∧
      T' (.pcKey vid) = T (.pcKey vid) ∧
      ¬ WarFreeAt (exec (gatedStep R vid) (fun _ => ()) T).2 j ∧
      (exec (gatedStep R vid) (fun _ => ()) T').1 = .failed "copy_bad_src" := by
  let R : Runtime Unit Unit Unit :=
    { caps := fun _ => false
      native := fun _ => ⟨0, fun _ _ _ => .error "no_native"⟩
      delegExt := fun _ => ⟨fun _ _ _ _ => pure (.error "no_deleg")⟩
      handoffExt := fun _ => ⟨fun _ _ _ _ => pure (.error "no_handoff")⟩ }
  let vid : Vid := 1
  let sSlot : Key := .frameSlot vid 1 "s"
  let xSlot : Key := .frameSlot vid 1 "x"
  let T : Store Key (Val Unit) := fun k =>
    if k = .pcKey vid then some (.pc ⟨1, 1⟩)
    else if k = .statusKey vid then some (.status .running)
    else if k = .frameLib vid 1 then some (.sym "main")
    else if k = .libInstr "main" 1 then some (.instr "=" 1 1)
    else if k = .libOperand "main" 1 .rd 1 then some (.ref 1 (.localSlot "s"))
    else if k = .libOperand "main" 1 .wr 1 then some (.ref 0 (.localSlot "s"))
    else if k = sSlot then some (.ptr xSlot)
    else if k = xSlot then some (.int 42)
    else none
  have hActs : (exec (gatedStep R vid) (fun _ => ()) T).2 =
      [.read (.stopKey vid) none,
       .read (.pcKey vid) (some (.pc ⟨1, 1⟩)),
       .read (.frameLib vid 1) (some (.sym "main")),
       .read (.libInstr "main" 1) (some (.instr "=" 1 1)),
       .read (.libOperand "main" 1 .rd 1) (some (.ref 1 (.localSlot "s"))),
       .read (.libOperand "main" 1 .wr 1) (some (.ref 0 (.localSlot "s"))),
       .read sSlot (some (.ptr xSlot)),
       .read xSlot (some (.int 42)),
       .write (.put sSlot (.int 42)),
       .write (.put (.pcKey vid) (.pc ⟨1, 2⟩))] := rfl
  let T' : Store Key (Val Unit) := (Write.put sSlot (.int 42)).apply T
  have hCrash : CrashAt T (exec (gatedStep R vid) (fun _ => ()) T).2 9 T' := by
    rw [hActs]
    exact crashAt_prefix T _ 9
  refine ⟨R, vid, T, T', 9, ?_, rfl, rfl, hCrash, rfl, ?_, rfl⟩
  · intro pi dec hpc hdec
    have hpi : pi = ⟨1, 1⟩ := by
      simp [T, vid] at hpc
      exact hpc.symm
    subst hpi
    have hdecEq : dec = ⟨"main", "=",
        [(.libOperand "main" 1 .rd 1, .ref 1 (.localSlot "s"))],
        [(.libOperand "main" 1 .wr 1, .ref 0 (.localSlot "s"))]⟩ := by
      simp [decodeInstrPure, readOperandsPure, T, vid, PC.valid] at hdec
      exact hdec.symm
    subst hdecEq
    refine ⟨?_, ?_⟩
    · intro x hx
      simp at hx
      subst hx
      refine ⟨?_, ?_⟩
      · intro k hk
        simp [resolveReadReads, derefChainReads, BaseRef.startKey, T, sSlot, xSlot, vid] at hk
        subst hk
        rfl
      · intro p hp
        simp [resolveReadPure, derefChainPure, BaseRef.startKey, T, sSlot, xSlot, vid] at hp
        subst hp
        rfl
    · intro y hy
      simp at hy
      subst hy
      refine ⟨by simp [resolveWriteReads, derefChainReads], ?_⟩
      intro q hq
      simp [resolveWritePure, derefChainPure, BaseRef.startKey, vid] at hq
      subst hq
      exact ⟨rfl, rfl⟩
  · intro hwf
    have hs := hwf sSlot (by rw [hActs]; decide)
    rw [hActs] at hs
    exact absurd hs (by decide)

/-- **Staged `Copy` via temporary (`copy_staged_via_temp_recovery`)**:
lowering a potentially self-aliased `Copy(rd1, wr1)` (including `p = q` or
`q ∈ resolveReadReads vid pi rd1 T`, such as `Copy(*s, s)`) into two consecutive
`Copy` instructions using a fresh local frame slot `SlotName` `tmpSlot`:
- Step 1 at `pi`: `Copy(rd1, .ref 0 (.localSlot tmpSlot))`
- Step 2 at `pi.next`: `Copy(.ref 0 (.localSlot tmpSlot), wr1)`

Assuming `tmp := .frameSlot vid pi.depth tmpSlot` is disjoint from `p`, `q`, and
the pointer-resolution chains of `rd1` and `wr1`, and `q` is not in `wr1`'s own
pointer chain or `/lib`:
1. Step 1 succeeds (`T₁ := after (gatedStep R vid) ι T`) and satisfies full
   crash dichotomy across *all* cuts `j`;
2. Step 2 from `T₁` succeeds (`T₂ := after (gatedStep R vid) ι T₁`) and ALSO
   satisfies full crash dichotomy across *all* cuts `j`; and
3. `T₂` holds `some val` at `q`, `some (.pc pi.next.next)` at `kappa`, and
   leaves every path outside `{tmp, q, .pcKey vid}` unchanged (`T₂ k = T k`). -/
theorem copy_staged_via_temp_recovery {R : Runtime D I E} {vid : Vid}
    {pi : PC} {fn : SymName} {tmpSlot : SlotName}
    {rd1 wr1 : Key × Val D} {p q : Key} {val : Val D}
    {ι : Nat → I} {T : Store Key (Val D)}
    (hpv : pi.valid = true)
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hLib : T (.frameLib vid pi.depth) = some (.sym fn))
    (hIns1 : T (.libInstr fn pi.row) = some (.instr "=" 1 1))
    (hOp1Rd : T (.libOperand fn pi.row .rd 1) = some rd1.2)
    (hrd1Key : rd1.1 = .libOperand fn pi.row .rd 1)
    (hOp1Wr : T (.libOperand fn pi.row .wr 1) = some (.ref 0 (.localSlot tmpSlot)))
    (hIns2 : T (.libInstr fn (pi.row + 1)) = some (.instr "=" 1 1))
    (hOp2Rd : T (.libOperand fn (pi.row + 1) .rd 1) = some (.ref 0 (.localSlot tmpSlot)))
    (hOp2Wr : T (.libOperand fn (pi.row + 1) .wr 1) = some wr1.2)
    (hwr1Key : wr1.1 = .libOperand fn (pi.row + 1) .wr 1)
    (hrd1Safe : ReadOpSafe vid pi T rd1)
    (hwr1Safe : WriteOpSafe vid pi T wr1)
    (hp : resolveReadPure vid pi rd1 T = some p)
    (hq : resolveWritePure vid pi wr1 T = some q)
    (hval : T p = some val)
    (hTmpNeP : Key.frameSlot vid pi.depth tmpSlot ≠ p)
    (hTmpNeQ : Key.frameSlot vid pi.depth tmpSlot ≠ q)
    (hTmpNotRd : Key.frameSlot vid pi.depth tmpSlot ∉ resolveReadReads vid pi rd1 T)
    (hTmpNotWr : Key.frameSlot vid pi.depth tmpSlot ∉ resolveWriteReads vid pi wr1 T)
    (hqNotWr : q ∉ resolveWriteReads vid pi wr1 T)
    (hqNotLib : isLibCodeKey q = false) :
    let tmp := Key.frameSlot vid pi.depth tmpSlot
    let T₁ := after (gatedStep R vid) ι T
    let T₂ := after (gatedStep R vid) ι T₁
    -- (1) Both staged steps succeed:
    (exec (gatedStep R vid) ι T).1 = .stepped pi.next [] ∧
    (exec (gatedStep R vid) ι T₁).1 = .stepped pi.next.next [] ∧
    -- (2) Final store holds `val` at `q` and preserves all other non-`tmp`/non-`kappa` keys:
    T₂ q = some val ∧
    T₂ (.pcKey vid) = some (.pc pi.next.next) ∧
    (∀ k, k ≠ tmp → k ≠ q → k ≠ .pcKey vid → T₂ k = T k) ∧
    -- (3) Step 1 satisfies crash dichotomy at every cut:
    (∀ j T', CrashAt T (exec (gatedStep R vid) ι T).2 j T' →
      ∃ jc₁,
        (j ≤ jc₁ ∧ WarFreeAt (exec (gatedStep R vid) ι T).2 j ∧
          after (gatedStep R vid) ι T' = T₁) ∨
        (jc₁ < j ∧ StoreAgree vid T' T₁)) ∧
    -- (4) Step 2 satisfies crash dichotomy at every cut:
    (∀ j T', CrashAt T₁ (exec (gatedStep R vid) ι T₁).2 j T' →
      ∃ jc₂,
        (j ≤ jc₂ ∧ WarFreeAt (exec (gatedStep R vid) ι T₁).2 j ∧
          after (gatedStep R vid) ι T' = T₂) ∨
        (jc₂ < j ∧ StoreAgree vid T' T₂)) := by
  dsimp only []
  let tmp := Key.frameSlot vid pi.depth tmpSlot
  let wrTmp : Key × Val D := (.libOperand fn pi.row .wr 1, .ref 0 (.localSlot tmpSlot))
  let rdTmp : Key × Val D := (.libOperand fn (pi.row + 1) .rd 1, .ref 0 (.localSlot tmpSlot))
  have hrd1Pair : (.libOperand fn pi.row .rd 1, rd1.2) = rd1 := by
    cases rd1; simp at hrd1Key; simp [hrd1Key]
  have hwr1Pair : (.libOperand fn (pi.row + 1) .wr 1, wr1.2) = wr1 := by
    cases wr1; simp at hwr1Key; simp [hwr1Key]
  have hdec1 : decodeInstrPure vid pi T = .ok ⟨fn, "=", [rd1], [wrTmp]⟩ := by
    simp [decodeInstrPure, readOperandsPure, hpv, hLib, hIns1, hOp1Rd, hOp1Wr, hrd1Pair, wrTmp]
  have hTmpResolve : resolveWritePure vid pi wrTmp T = some tmp := rfl
  have hTmpNotForb : ForbiddenKey vid pi.depth tmp = false := by
    have hge1 : 1 ≤ pi.depth := (PC.valid_iff pi).1 hpv |>.1
    simp [tmp, ForbiddenKey, isDeadFrameKey, isThreadControlKey]
    omega
  have hStep1P : StepDiscipline vid T := by
    intro pi0 dec0 hpc0 hdec0
    rw [hpc] at hpc0; injection hpc0 with hpc0; injection hpc0 with hpi0; subst hpi0
    rw [hdec1] at hdec0; injection hdec0 with hdec0; subst hdec0
    refine ⟨?_, ?_⟩
    · intro x hx; simp at hx; subst hx; exact hrd1Safe
    · intro y hy; simp at hy; subst hy
      refine ⟨by simp [wrTmp, resolveWriteReads, derefChainReads], ?_⟩
      intro q0 hq0
      rw [hTmpResolve] at hq0; injection hq0 with hq0; subst hq0
      exact ⟨hTmpNotForb, rfl⟩
  have hdec1Ex : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 =
      .ok ⟨fn, "=", [rd1], [wrTmp]⟩ := by
    rw [exec_decodeInstr_fst]; exact hdec1
  have hStep1Run : (exec (gatedStep R vid) ι T).1 = .stepped pi.next [] ∧
      after (gatedStep R vid) ι T =
        (Write.put (.pcKey vid) (.pc pi.next)).apply ((Write.put tmp val).apply T) := by
    unfold gatedStep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read, hrho]
    rw [exec_prog_bind_fst, after_bind]
    have hCore : (exec (stepCore R vid) ι T).1 = .ok (pi.next, []) ∧
        after (stepCore R vid) ι T =
          (Write.put (.pcKey vid) (.pc pi.next)).apply ((Write.put tmp val).apply T) := by
      unfold stepCore
      simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read, hpc]
      rw [exec_prog_bind_fst, after_bind, hdec1Ex, after_decodeInstr]
      dsimp only []
      unfold dispatchInstr
      simp only [beq_self_eq_true, ite_true]
      unfold copyRuleProg
      dsimp only []
      rw [exec_bind_fst, after_bind_op, exec_resolveRead, after_resolveRead, hp]
      dsimp only []
      rw [exec_bind_fst, after_bind_op, exec_resolveWrite, after_resolveWrite, hTmpResolve]
      dsimp only []
      simp [Prog.bind_eq, Prog.rd_bind, Prog.wr_bind, Prog.pure_eq, hval]
    rw [hCore.1, hCore.2]
    simp [finalizeStep]
  let T₁ := after (gatedStep R vid) ι T
  have hT₁Eq : T₁ = (Write.put (.pcKey vid) (.pc pi.next)).apply ((Write.put tmp val).apply T) :=
    hStep1Run.2
  have hT₁Other : ∀ k, k ≠ .pcKey vid → k ≠ tmp → T₁ k = T k := by
    intro k hnePc hneTmp
    rw [hT₁Eq]
    simp [Write.apply, Write.affects, hnePc, hneTmp]
  have hpv2 : pi.next.valid = true := PC.next_valid hpv
  have hDepth2 : pi.next.depth = pi.depth := PC.next_depth pi
  have hRow2 : pi.next.row = pi.row + 1 := PC.next_row pi
  have hT₁Rho : T₁ (.stopKey vid) = none := by
    rw [hT₁Other _ (by intro h; cases h) (by simp [tmp])]
    exact hrho
  have hT₁Pc : T₁ (.pcKey vid) = some (.pc pi.next) := by
    rw [hT₁Eq]; simp [Write.apply, Write.affects, Write.newVal]
  have hT₁Tmp : T₁ tmp = some val := by
    rw [hT₁Eq]
    have hTmpNePc : tmp ≠ .pcKey vid := by simp [tmp]
    simp [Write.apply, Write.affects, Write.newVal, hTmpNePc]
  have hdec2 : decodeInstrPure vid pi.next T₁ = .ok ⟨fn, "=", [rdTmp], [wr1]⟩ := by
    have hLib1 : T₁ (.frameLib vid pi.depth) = some (.sym fn) := by
      rw [hT₁Other _ (by intro h; cases h) (by simp [tmp])]
      exact hLib
    have hIns2_1 : T₁ (.libInstr fn (pi.row + 1)) = some (.instr "=" 1 1) := by
      rw [hT₁Other _ (by intro h; cases h) (by simp [tmp])]
      exact hIns2
    have hOp2Rd_1 : T₁ (.libOperand fn (pi.row + 1) .rd 1) =
        some (.ref 0 (.localSlot tmpSlot)) := by
      rw [hT₁Other _ (by intro h; cases h) (by simp [tmp])]
      exact hOp2Rd
    have hOp2Wr_1 : T₁ (.libOperand fn (pi.row + 1) .wr 1) = some wr1.2 := by
      rw [hT₁Other _ (by intro h; cases h) (by simp [tmp])]
      exact hOp2Wr
    unfold decodeInstrPure
    rw [hpv2]
    simp [PC.next, readOperandsPure, hLib1, hIns2_1, hOp2Rd_1, hOp2Wr_1, rdTmp, hwr1Pair]
  -- `wr1` resolves identically in `T₁` and `T` because neither `.pcKey vid` nor `tmp` is in `resolveWriteReads`:
  have hWrEqExec : ∀ (ιw : Nat → I),
      exec (resolveWrite (I := I) (E := E) vid pi.next wr1) ιw T₁ =
        exec (resolveWrite (I := I) (E := E) vid pi wr1) ιw T := by
    intro ιw
    have hPiEq : resolveWrite (I := I) (E := E) vid pi.next wr1 =
        resolveWrite (I := I) (E := E) vid pi wr1 := by
      rcases wr1 with ⟨wk, wv⟩
      cases wv <;> simp [resolveWrite, BaseRef.startKey]
    rw [hPiEq]
    apply exec_congr _ _ T T₁ (fun k => k = .pcKey vid ∨ k = tmp)
    · intro k hk
      exact hT₁Other k (fun h => hk (Or.inl h)) (fun h => hk (Or.inr h))
    · intro k hexp
      obtain ⟨v, hm⟩ := Act.reads_of_exposed hexp
      rw [exec_resolveWrite] at hm
      simp only [List.mem_map] at hm
      obtain ⟨r, hr, hAct⟩ := hm
      cases hAct
      rintro (rfl | rfl)
      · have hf := hwr1Safe.1 _ hr
        simp [ForbiddenKey, isThreadControlKey] at hf
      · exact hTmpNotWr hr
  have hWrPure2 : resolveWritePure vid pi.next wr1 T₁ = some q := by
    have h1 := congrArg Prod.fst (hWrEqExec ι)
    rw [exec_resolveWrite, exec_resolveWrite] at h1
    exact h1.trans hq
  have hWrReads2 : resolveWriteReads vid pi.next wr1 T₁ = resolveWriteReads vid pi wr1 T := by
    rcases wr1 with ⟨wk, wv⟩
    cases wv with
    | ref hops base =>
      have h2 := congrArg Prod.snd (hWrEqExec ι)
      rw [exec_resolveWrite, exec_resolveWrite] at h2
      have map_fst_eq : ∀ {l₁ l₂ : List Key} {S₁ S₂ : Store Key (Val D)},
          (l₁.map fun r => (Act.read r (S₁ r) : Act Key (Val D) I E)) =
          (l₂.map fun r => (Act.read r (S₂ r) : Act Key (Val D) I E)) → l₁ = l₂ := by
        intro l₁
        induction l₁ with
        | nil => intro l₂ _ _ h; cases l₂ <;> simp at h; rfl
        | cons a l₁ ih =>
          intro l₂ _ _ h
          cases l₂ with
          | nil => simp at h
          | cons b l₂ =>
            injection h with hhd htl
            injection hhd with hab _
            subst hab
            exact congrArg (List.cons a) (ih htl)
      exact map_fst_eq h2
    | _ => rfl
  have hRdTmpPure : resolveReadPure vid pi.next rdTmp T₁ = some tmp := by
    simp [rdTmp, resolveReadPure, derefChainPure, BaseRef.startKey, tmp]
  have hRdTmpReads : resolveReadReads vid pi.next rdTmp T₁ = [] := rfl
  have hStep2P : StepDiscipline vid T₁ := by
    intro pi0 dec0 hpc0 hdec0
    rw [hT₁Pc] at hpc0; injection hpc0 with hpc0; injection hpc0 with hpi0; subst hpi0
    rw [hdec2] at hdec0; injection hdec0 with hdec0; subst hdec0
    refine ⟨?_, ?_⟩
    · intro x hx; simp at hx; subst hx
      refine ⟨by simp [hRdTmpReads], ?_⟩
      intro p0 hp0
      rw [hRdTmpPure] at hp0; injection hp0 with hp0; subst hp0
      simpa [hDepth2] using hTmpNotForb
    · intro y hy; simp at hy; subst hy
      refine ⟨?_, ?_⟩
      · intro r hr
        rw [hWrReads2] at hr
        simpa [hDepth2] using hwr1Safe.1 r hr
      · intro q0 hq0
        rw [hWrPure2] at hq0; injection hq0 with hq0; subst hq0
        simpa [hDepth2] using hwr1Safe.2 q hq
  have hdec2Ex : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi.next) ι T₁).1 =
      .ok ⟨fn, "=", [rdTmp], [wr1]⟩ := by
    rw [exec_decodeInstr_fst]; exact hdec2
  have hStep2Run : (exec (gatedStep R vid) ι T₁).1 = .stepped pi.next.next [] ∧
      after (gatedStep R vid) ι T₁ =
        (Write.put (.pcKey vid) (.pc pi.next.next)).apply ((Write.put q val).apply T₁) := by
    unfold gatedStep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read, hT₁Rho]
    rw [exec_prog_bind_fst, after_bind]
    have hCore : (exec (stepCore R vid) ι T₁).1 = .ok (pi.next.next, []) ∧
        after (stepCore R vid) ι T₁ =
          (Write.put (.pcKey vid) (.pc pi.next.next)).apply ((Write.put q val).apply T₁) := by
      unfold stepCore
      simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read, hT₁Pc]
      rw [exec_prog_bind_fst, after_bind, hdec2Ex, after_decodeInstr]
      dsimp only []
      unfold dispatchInstr
      simp only [beq_self_eq_true, ite_true]
      unfold copyRuleProg
      dsimp only []
      rw [exec_bind_fst, after_bind_op, exec_resolveRead, after_resolveRead, hRdTmpPure]
      dsimp only []
      rw [exec_bind_fst, after_bind_op, exec_resolveWrite, after_resolveWrite, hWrPure2]
      dsimp only []
      simp [Prog.bind_eq, Prog.rd_bind, Prog.wr_bind, Prog.pure_eq, hT₁Tmp]
    rw [hCore.1, hCore.2]
    simp [finalizeStep]
  let T₂ := after (gatedStep R vid) ι T₁
  have hqNePc : q ≠ .pcKey vid := by
    rintro rfl
    have hf := (hwr1Safe.2 _ hq).1
    simp [ForbiddenKey, isThreadControlKey] at hf
  refine ⟨hStep1Run.1, hStep2Run.1, ?_, ?_, ?_, ?_, ?_⟩
  · -- `T₂ q = some val`
    change (after (gatedStep R vid) ι T₁) q = some val
    rw [hStep2Run.2]
    simp [Write.apply, Write.affects, Write.newVal, hqNePc]
  · -- `T₂ (.pcKey vid) = some (.pc pi.next.next)`
    change (after (gatedStep R vid) ι T₁) (.pcKey vid) = some (.pc pi.next.next)
    rw [hStep2Run.2]
    simp [Write.apply, Write.affects, Write.newVal]
  · -- Preservation outside `{tmp, q, .pcKey vid}`
    intro k hkTmp hkQ hkPc
    change (after (gatedStep R vid) ι T₁) k = T k
    rw [hStep2Run.2]
    simp only [Write.apply, Write.affects, decide_eq_true_eq, hkPc, hkQ, ite_false]
    exact hT₁Other k hkPc hkTmp
  · -- Step 1 crash dichotomy
    intro j T' hc1
    have hNonAl1 : CopyNonAliased vid pi T rd1 wrTmp p tmp :=
      ⟨hTmpNeP, hTmpNotRd, by simp [wrTmp, resolveWriteReads, derefChainReads], rfl⟩
    obtain ⟨jc₁, h1 | h2⟩ := copy_gatedStep_crash_dichotomy hrho hpc hdec1 rfl rfl rfl
      hStep1P hp hTmpResolve hNonAl1 hStep1Run.1 hc1
    · exact ⟨jc₁, Or.inl ⟨h1.1, h1.2.1, h1.2.2.2.2.2⟩⟩
    · exact ⟨jc₁, Or.inr ⟨h2.1, h2.2.2⟩⟩
  · -- Step 2 crash dichotomy
    intro j T' hc2
    have hNonAl2 : CopyNonAliased vid pi.next T₁ rdTmp wr1 tmp q :=
      ⟨hTmpNeQ.symm, by simp [hRdTmpReads], by rw [hWrReads2]; exact hqNotWr, hqNotLib⟩
    obtain ⟨jc₂, h1 | h2⟩ := copy_gatedStep_crash_dichotomy hT₁Rho hT₁Pc hdec2 rfl rfl rfl
      hStep2P hRdTmpPure hWrPure2 hNonAl2 hStep2Run.1 hc2
    · exact ⟨jc₂, Or.inl ⟨h1.1, h1.2.1, h1.2.2.2.2.2⟩⟩
    · exact ⟨jc₂, Or.inr ⟨h2.1, h2.2.2⟩⟩

/-! ### 3. Crash-Ordered `Call` and `UserCall` (and Reversed-Order Counterexample) -/

/-- Under operand safety, `bindChild` satisfies global `StepWarFree` across *all*
cuts (including mid-`delWhere` and every prefix of child-frame writes), so
`scoped_crash_recovery` and `scoped_crash_recovery_write_prefix` apply directly
to `bindChild`. -/
theorem bindChild_stepWarFree {vid : Vid} {pi : PC} {callee : SymName}
    {rdArgs wrArgs : List (Key × Val D)} {ι : Nat → I} {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdArgs, ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrArgs, WriteOpSafe vid pi T y) :
    StepWarFree (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T := by
  have hAv := bindChild_child_footprint (I := I) (E := E)
    (vid := vid) (pi := pi) (callee := callee) hrd hwr (ι := ι)
  apply StepWarFree.of_noReadWriteOverlap
  intro k ⟨v, hm⟩
  have hNotChild : inFrame vid (pi.depth + 1) k = false := by
    cases hc : inFrame vid (pi.depth + 1) k with
    | false => rfl
    | true => exact absurd hc (hAv.1 k v hm)
  exact hAv.not_touches hNotChild

/-- **Crash recovery for crash-ordered `Call` at `gatedStep` level**:
because `callRuleProg` writes the entire child frame `[pi.depth + 1]` via
`bindChild` *before* committing `put(kappa, pi.entry)`, under `StepDiscipline`:
- every pre-commit crash cut `j ≤ j_commit` (including mid-`delWhere` and any
  prefix of child-frame writes) satisfies `WarFreeAt` and replays `gatedStep`
  cleanly to `after (gatedStep R vid) ι T`;
- every post-commit cut agrees with `after (gatedStep R vid) ι T`. -/
theorem call_gatedStep_crash_dichotomy {R : Runtime D I E} {vid : Vid}
    {pi pi' : PC} {dec : DecodedInstr D} {evs : List E}
    {ι : Nat → I} {T T' : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hop : dec.op = "call")
    (hP : StepDiscipline vid T)
    (hstep : (exec (gatedStep R vid) ι T).1 = .stepped pi' evs)
    {j : Nat} (hc : CrashAt T (exec (gatedStep R vid) ι T).2 j T') :
    ∃ j_commit,
      (j ≤ j_commit ∧
        WarFreeAt (exec (gatedStep R vid) ι T).2 j ∧
        T' (.pcKey vid) = T (.pcKey vid) ∧
        exec (gatedStep R vid) ι T' = exec (gatedStep R vid) ι T ∧
        after (gatedStep R vid) ι T' =
          applyAll T' (Act.writes ((exec (gatedStep R vid) ι T).2.drop j)) ∧
        after (gatedStep R vid) ι T' = after (gatedStep R vid) ι T) ∨
      (j_commit < j ∧
        T' (.pcKey vid) = some (.pc pi') ∧
        StoreAgree vid T' (after (gatedStep R vid) ι T)) := by
  have hpv : pi.valid = true := by
    unfold decodeInstrPure at hdec
    cases hv : pi.valid
    · rw [hv] at hdec; simp at hdec
    · rfl
  obtain ⟨hrdSafe, hwrSafe⟩ := hP pi dec hpc hdec
  let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hdisp : (exec (dispatchInstr R vid pi dec) ιd T).1 = .ok (pi', evs) := by
    unfold gatedStep at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho] at hstep
    rw [exec_prog_bind_fst] at hstep
    unfold stepCore at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read] at hstep
    rw [exec_prog_bind_fst, hdecEx, after_decodeInstr] at hstep
    dsimp only [] at hstep
    cases hd : (exec (dispatchInstr R vid pi dec) ιd T).1 with
    | error err => rw [hd] at hstep; simp [finalizeStep] at hstep
    | ok pair =>
      rcases pair with ⟨p2, e2⟩
      rw [hd] at hstep; simp [finalizeStep] at hstep
      rcases hstep with ⟨rfl, rfl⟩
      rfl
  obtain ⟨hActsG, hPcExp⟩ := exec_gatedStep_stepped_actions hrho hpc hdec hdisp
  have hDispEq : exec (dispatchInstr R vid pi dec) ιd T =
      exec (callRuleProg vid pi dec.rdOps dec.wrOps) ιd T := by
    unfold dispatchInstr
    simp [hop]
  rw [hDispEq] at hdisp hActsG
  unfold callRuleProg at hdisp hActsG
  match hrdOps : dec.rdOps with
  | [] => rw [hrdOps] at hdisp; simp at hdisp
  | fnOp :: argRdOps =>
    rw [hrdOps] at hdisp hActsG hrdSafe
    have hFnSafe : ReadOpSafe vid pi T fnOp := hrdSafe fnOp List.mem_cons_self
    have hArgsSafe : ∀ x ∈ argRdOps, ReadOpSafe vid pi T x :=
      fun x hx => hrdSafe x (List.mem_cons_of_mem _ hx)
    dsimp only [] at hdisp hActsG
    rw [exec_bind_fst, exec_resolveRead, after_resolveRead] at hdisp
    rw [exec_bind_snd, exec_resolveRead, after_resolveRead] at hActsG
    cases hpFn : resolveReadPure vid pi fnOp T with
    | none => rw [hpFn] at hdisp; simp at hdisp
    | some pFn =>
      rw [hpFn] at hdisp hActsG
      dsimp only [] at hdisp hActsG
      simp only [Prog.bind_eq, Prog.rd_bind, exec_read] at hdisp hActsG
      cases hvFn : T pFn with
      | none => rw [hvFn] at hdisp; simp at hdisp
      | some vFn =>
        rw [hvFn] at hdisp hActsG
        cases vFn <;> try (simp at hdisp; done)
        rename_i callee
        dsimp only [] at hdisp hActsG
        rw [exec_prog_bind_fst] at hdisp
        rw [exec_prog_bind_snd] at hActsG
        cases hb : (exec (bindChild (I := I) (E := E) vid pi callee argRdOps dec.wrOps) _ T).1 with
        | error _ => rw [hb] at hdisp; simp at hdisp
        | ok u =>
          cases u
          rw [hb] at hdisp hActsG
          dsimp only [] at hdisp hActsG
          simp only [Prog.wr_bind, exec_write, Prog.pure_eq, exec_ret] at hdisp hActsG
          cases hdisp
          let ιb := shiftInputs (resolveRead (I := I) (E := E) vid pi fnOp) ιd T
          let pre := [.read (.stopKey vid) none, .read (.pcKey vid) (some (.pc pi))] ++
            (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 ++
            (resolveReadReads vid pi fnOp T).map (fun r => Act.read r (T r)) ++
            [.read pFn (some (.sym callee))] ++
            (exec (bindChild (I := I) (E := E) vid pi callee argRdOps dec.wrOps) ιb T).2
          have hSplit : (exec (gatedStep R vid) ι T).2 =
              pre ++ [.write (.put (.pcKey vid) (.pc pi.entry))] ++ [] := by
            simpa [pre, ιb, List.append_assoc] using hActsG
          have hBindAv := bindChild_child_footprint (I := I) (E := E)
            (vid := vid) (pi := pi) (callee := callee) hArgsSafe hwrSafe (ι := ιb)
          have hPreTouchesChild : ∀ k, touches (Act.writes pre) k = true →
              inFrame vid (pi.depth + 1) k = true := by
            intro k ht
            have hPreW : Act.writes pre =
                Act.writes (exec (bindChild (I := I) (E := E) vid pi callee argRdOps dec.wrOps) ιb T).2 := by
              simp [pre, Act.writes_append, writes_decodeInstr_nil, writes_map_reads_nil]
            rw [hPreW] at ht
            cases hin : inFrame vid (pi.depth + 1) k with
            | true => rfl
            | false =>
              rw [hBindAv.not_touches hin] at ht
              cases ht
          have hNoChildRead : ∀ k, Act.reads (exec (gatedStep R vid) ι T).2 k →
              inFrame vid (pi.depth + 1) k = false := by
            rintro k ⟨v, hm⟩
            rw [hSplit] at hm
            simp only [pre, List.append_nil, List.mem_append, List.mem_cons,
              List.not_mem_nil, or_false, List.mem_map] at hm
            rcases hm with (((((hEq | hEq) | hmDec) | ⟨r, hr, hEq⟩) | hEq) | hmBind) | hEq
            · cases hEq; rfl
            · cases hEq; rfl
            · rcases decodeInstr_reads_kind hmDec with rfl | hCode
              · simp [inFrame]
              · cases k <;> simp_all [isLibCodeKey, inFrame]
            · cases hEq
              exact not_inChild_of_not_forbidden (hFnSafe.1 _ hr)
            · cases hEq
              exact not_inChild_of_not_forbidden (hFnSafe.2 pFn hpFn)
            · cases hin : inFrame vid (pi.depth + 1) k with
              | false => rfl
              | true => exact absurd hin (hBindAv.1 k v hmBind)
            · cases hEq
          refine ⟨pre.length, commitSplit_crash_dichotomy hSplit ?_ hPcExp ?_ hc⟩
          · intro k hexp
            cases ht : touches (Act.writes pre) k with
            | false => rfl
            | true =>
              have hch := hPreTouchesChild k ht
              rw [hNoChildRead k (Act.reads_of_exposed hexp)] at hch
              cases hch
          · intro w hw; simp at hw

/-- **Crash recovery for crash-ordered `UserCall` (`rwfunc` direct call) at
`gatedStep` level**: identical guarantee to `call_gatedStep_crash_dichotomy`. -/
theorem userCall_gatedStep_crash_dichotomy {R : Runtime D I E} {vid : Vid}
    {pi pi' : PC} {dec : DecodedInstr D} {evs : List E}
    {ι : Nat → I} {T T' : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hNotCore : isCoreOp dec.op = false)
    (hNotCap : R.caps dec.op = false)
    (hNoDeleg : ∀ rt, T (.delegEntry dec.op) ≠ some (.delegTarget rt))
    (hHdr : T (.libHeader dec.op) = some .rwfuncHdr)
    (hP : StepDiscipline vid T)
    (hstep : (exec (gatedStep R vid) ι T).1 = .stepped pi' evs)
    {j : Nat} (hc : CrashAt T (exec (gatedStep R vid) ι T).2 j T') :
    ∃ j_commit,
      (j ≤ j_commit ∧
        WarFreeAt (exec (gatedStep R vid) ι T).2 j ∧
        T' (.pcKey vid) = T (.pcKey vid) ∧
        exec (gatedStep R vid) ι T' = exec (gatedStep R vid) ι T ∧
        after (gatedStep R vid) ι T' =
          applyAll T' (Act.writes ((exec (gatedStep R vid) ι T).2.drop j)) ∧
        after (gatedStep R vid) ι T' = after (gatedStep R vid) ι T) ∨
      (j_commit < j ∧
        T' (.pcKey vid) = some (.pc pi') ∧
        StoreAgree vid T' (after (gatedStep R vid) ι T)) := by
  obtain ⟨hrdSafe, hwrSafe⟩ := hP pi dec hpc hdec
  let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hdisp : (exec (dispatchInstr R vid pi dec) ιd T).1 = .ok (pi', evs) := by
    unfold gatedStep at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho] at hstep
    rw [exec_prog_bind_fst] at hstep
    unfold stepCore at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read] at hstep
    rw [exec_prog_bind_fst, hdecEx, after_decodeInstr] at hstep
    dsimp only [] at hstep
    cases hd : (exec (dispatchInstr R vid pi dec) ιd T).1 with
    | error err => rw [hd] at hstep; simp [finalizeStep] at hstep
    | ok pair =>
      rcases pair with ⟨p2, e2⟩
      rw [hd] at hstep; simp [finalizeStep] at hstep
      rcases hstep with ⟨rfl, rfl⟩
      rfl
  obtain ⟨hActsG, hPcExp⟩ := exec_gatedStep_stepped_actions hrho hpc hdec hdisp
  have hNotEq : (dec.op == "=") = false := by
    cases h : (dec.op == "=") <;> simp_all [isCoreOp]
  have hNotGoto : (dec.op == "goto") = false := by
    cases h : (dec.op == "goto") <;> simp_all [isCoreOp]
  have hNotBr : (dec.op == "br") = false := by
    cases h : (dec.op == "br") <;> simp_all [isCoreOp]
  have hNotCall : (dec.op == "call") = false := by
    cases h : (dec.op == "call") <;> simp_all [isCoreOp]
  have hNotRet : (dec.op == "return") = false := by
    cases h : (dec.op == "return") <;> simp_all [isCoreOp]
  have hDispActs : (exec (dispatchInstr R vid pi dec) ιd T).1 =
      (exec (userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps) ιd T).1 ∧
      (exec (dispatchInstr R vid pi dec) ιd T).2 =
        [.read (.delegEntry dec.op) (T (.delegEntry dec.op)),
         .read (.libHeader dec.op) (some .rwfuncHdr)] ++
        (exec (userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps) ιd T).2 := by
    unfold dispatchInstr
    simp only [hNotEq, hNotGoto, hNotBr, hNotCall, hNotRet, hNotCap,
      Bool.false_eq_true, ite_false, Prog.bind_eq, Prog.rd_bind, exec_read]
    cases hvDel : T (.delegEntry dec.op) with
    | none =>
      simp [hHdr]
    | some vDel =>
      cases vDel with
      | delegTarget rt => exact absurd hvDel (hNoDeleg rt)
      | _ => simp [hHdr]
  rw [hDispActs.1] at hdisp
  rw [hDispActs.2] at hActsG
  unfold userCallRuleProg at hdisp hActsG
  rw [exec_bind_fst] at hdisp
  rw [exec_bind_snd] at hActsG
  cases hb : (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ιd T).1 with
  | error _ => rw [hb] at hdisp; simp at hdisp
  | ok u =>
    cases u
    rw [hb] at hdisp hActsG
    dsimp only [] at hdisp hActsG
    simp only [Prog.bind_eq, Prog.wr_bind, exec_write, Prog.pure_eq, exec_ret] at hdisp hActsG
    cases hdisp
    let pre := [.read (.stopKey vid) none, .read (.pcKey vid) (some (.pc pi))] ++
      (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 ++
      [.read (.delegEntry dec.op) (T (.delegEntry dec.op)),
       .read (.libHeader dec.op) (some .rwfuncHdr)] ++
      (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ιd T).2
    have hSplit : (exec (gatedStep R vid) ι T).2 =
        pre ++ [.write (.put (.pcKey vid) (.pc pi.entry))] ++ [] := by
      simpa [pre, List.append_assoc] using hActsG
    have hBindAv := bindChild_child_footprint (I := I) (E := E)
      (vid := vid) (pi := pi) (callee := dec.op) hrdSafe hwrSafe (ι := ιd)
    have hPreTouchesChild : ∀ k, touches (Act.writes pre) k = true →
        inFrame vid (pi.depth + 1) k = true := by
      intro k ht
      have hPreW : Act.writes pre =
          Act.writes (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ιd T).2 := by
        simp [pre, Act.writes_append, writes_decodeInstr_nil]
      rw [hPreW] at ht
      cases hin : inFrame vid (pi.depth + 1) k with
      | true => rfl
      | false =>
        rw [hBindAv.not_touches hin] at ht
        cases ht
    have hNoChildRead : ∀ k, Act.reads (exec (gatedStep R vid) ι T).2 k →
        inFrame vid (pi.depth + 1) k = false := by
      rintro k ⟨v, hm⟩
      rw [hSplit] at hm
      simp only [pre, List.append_nil, List.mem_append, List.mem_cons,
        List.not_mem_nil, or_false] at hm
      rcases hm with ((((hEq | hEq) | hmDec) | (hEq | hEq)) | hmBind) | hEq
      · cases hEq; rfl
      · cases hEq; rfl
      · rcases decodeInstr_reads_kind hmDec with rfl | hCode
        · simp [inFrame]
        · cases k <;> simp_all [isLibCodeKey, inFrame]
      · cases hEq; rfl
      · cases hEq; rfl
      · cases hin : inFrame vid (pi.depth + 1) k with
        | false => rfl
        | true => exact absurd hin (hBindAv.1 k v hmBind)
      · cases hEq
    refine ⟨pre.length, commitSplit_crash_dichotomy hSplit ?_ hPcExp ?_ hc⟩
    · intro k hexp
      cases ht : touches (Act.writes pre) k with
      | false => rfl
      | true =>
        have hch := hPreTouchesChild k ht
        rw [hNoChildRead k (Act.reads_of_exposed hexp)] at hch
        cases hch
    · intro w hw; simp at hw

/-- Hypothetical *reversed-order* `Call` step program that writes `kappa := pi.entry`
*before* populating the child frame via `bindChild` (§4, Theorem 4). -/
def reversedCallStep (vid : Vid) : Prog Key (Val D) I E (Except ErrMsg PC) := do
  let vPc ← Prog.rd (.pcKey vid)
  match vPc with
  | some (.pc pi) =>
      let dRes ← decodeInstr vid pi
      match dRes with
      | .ok dec =>
          match dec.rdOps with
          | fnOp :: argRdOps =>
              let pFn? ← resolveRead vid pi fnOp
              match pFn? with
              | some pFn =>
                  let vFn ← Prog.rd pFn
                  match vFn with
                  | some (.sym callee) =>
                      Prog.wr (.put (.pcKey vid) (.pc pi.entry))
                      let bRes ← bindChild vid pi callee argRdOps dec.wrOps
                      match bRes with
                      | .ok () => pure (.ok pi.entry)
                      | .error err => pure (.error err)
                  | _ => pure (.error "call_bad_fn_sym")
              | none => pure (.error "call_bad_fn_ref")
          | [] => pure (.error "call_arity")
      | .error err => pure (.error err)
  | _ => pure (.error "missing_pc")

/-- **Counterexample for reversed-order `Call`**: writing `kappa := pi.entry`
before `bindChild` creates a crash cut `j` at which `WarFreeAt` fails, the crash
store `T'` violates `CommitComplete` (child `[2]/..lib` is uninitialized), and
restarting `reversedCallStep` (or `gatedStep`) from `T'` fails with
`"missing_frame_lib"`. -/
theorem reversed_call_crash_counterexample :
    ∃ (vid : Vid) (T T' : Store Key (Val Unit)) (j : Nat),
      CommitComplete vid T ∧
      (exec (reversedCallStep (I := Unit) (E := Unit) vid) (fun _ => ()) T).1 = .ok ⟨2, 1⟩ ∧
      CrashAt T (exec (reversedCallStep (I := Unit) (E := Unit) vid) (fun _ => ()) T).2 j T' ∧
      ¬ WarFreeAt (exec (reversedCallStep (I := Unit) (E := Unit) vid) (fun _ => ()) T).2 j ∧
      ¬ CommitComplete vid T' ∧
      (exec (reversedCallStep (I := Unit) (E := Unit) vid) (fun _ => ()) T').1 =
        .error "missing_frame_lib" := by
  let vid : Vid := 1
  let fnSlot : Key := .libOperand "main" 1 .rd 1
  let T : Store Key (Val Unit) := fun k =>
    if k = .pcKey vid then some (.pc ⟨1, 1⟩)
    else if k = .statusKey vid then some (.status .running)
    else if k = .frameLib vid 1 then some (.sym "main")
    else if k = .frameCallPc vid 1 then some (.pc ⟨1, 1⟩)
    else if k = .libSig "main" then some (.sig 0 0)
    else if k = .libInstr "main" 1 then some (.instr "call" 1 0)
    else if k = fnSlot then some (.sym "sub")
    else if k = .libSig "sub" then some (.sig 0 0)
    else none
  have hC : CommitComplete vid T := by
    refine ⟨⟨1, 1⟩, rfl, rfl, ?_, ?_⟩
    · intro e h1 hle
      dsimp only [] at hle
      have he : e = 1 := by omega
      subst he
      refine ⟨⟨"main", 0, 0, rfl, rfl, ?_, ?_⟩, ⟨⟨1, 1⟩, rfl, rfl⟩⟩
      · intro j hj1 hj0; omega
      · intro j hj1 hj0; omega
    · intro e h2 hle
      dsimp only [] at hle
      omega
  let as := (exec (reversedCallStep (D := Unit) (I := Unit) (E := Unit) vid) (fun _ => ()) T).2
  let T' : Store Key (Val Unit) := (Write.put (.pcKey vid) (.pc ⟨2, 1⟩)).apply T
  have hEqT' : applyAll T (Act.writes (as.take 6)) = T' := rfl
  have hCrash : CrashAt T as 6 T' := hEqT' ▸ crashAt_prefix T as 6
  refine ⟨vid, T, T', 6, hC, rfl, hCrash, ?_, ?_, rfl⟩
  · intro hwf
    have hp := hwf (.pcKey vid) rfl
    exact absurd hp (by decide)
  · rintro ⟨pi', hpc', _, hFr, _⟩
    have hpi' : pi' = ⟨2, 1⟩ := by
      have h1 : T' (.pcKey vid) = some (.pc ⟨2, 1⟩) := rfl
      rw [h1] at hpc'
      injection hpc' with hEq
      injection hEq with hEq'
      exact hEq'.symm
    subst hpi'
    obtain ⟨⟨fn, _, _, hLib2, _⟩, _⟩ := hFr 2 (by omega) (by decide)
    have hNone : T' (.frameLib vid 2) = none := rfl
    rw [hNone] at hLib2
    cases hLib2

/-! ### 4. Crash-Ordered `Return` (Subroutine, Mid-`delWhere`, Root, and Reversed Counterexample) -/

/-- **Crash recovery for crash-ordered subroutine `Return` (`1 < pi.depth`)**:
`returnRuleProg` writes `put(kappa, r)` *before* `delWhere (inFrame vid pi.depth)`:
- every pre-commit cut `j ≤ j_commit` has zero writes before `put(kappa, r)`,
  satisfies `WarFreeAt`, and replays `gatedStep` cleanly to
  `after (gatedStep R vid) ι T`;
- every post-commit cut `j > j_commit` (including `j = j_commit + 1`, a crash
  *in the middle* of `delWhere (inFrame vid pi.depth)` leaving an arbitrary
  subset of frame `pi.depth` deleted!) holds `T'(kappa) = some (.pc r)` and
  satisfies `StoreAgree vid T' (after (gatedStep R vid) ι T)`, because
  `r.depth + 1 = pi.depth` makes frame `pi.depth` dead under `r.depth`. -/
theorem return_gatedStep_crash_dichotomy {R : Runtime D I E} {vid : Vid}
    {pi pi' : PC} {dec : DecodedInstr D} {evs : List E}
    {ι : Nat → I} {T T' : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hop : dec.op = "return")
    (hd : 1 < pi.depth)
    (hstep : (exec (gatedStep R vid) ι T).1 = .stepped pi' evs)
    {j : Nat} (hc : CrashAt T (exec (gatedStep R vid) ι T).2 j T') :
    ∃ j_commit,
      (j ≤ j_commit ∧
        WarFreeAt (exec (gatedStep R vid) ι T).2 j ∧
        T' (.pcKey vid) = T (.pcKey vid) ∧
        exec (gatedStep R vid) ι T' = exec (gatedStep R vid) ι T ∧
        after (gatedStep R vid) ι T' =
          applyAll T' (Act.writes ((exec (gatedStep R vid) ι T).2.drop j)) ∧
        after (gatedStep R vid) ι T' = after (gatedStep R vid) ι T) ∨
      (j_commit < j ∧
        T' (.pcKey vid) = some (.pc pi') ∧
        StoreAgree vid T' (after (gatedStep R vid) ι T)) := by
  let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hdisp : (exec (dispatchInstr R vid pi dec) ιd T).1 = .ok (pi', evs) := by
    unfold gatedStep at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho] at hstep
    rw [exec_prog_bind_fst] at hstep
    unfold stepCore at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read] at hstep
    rw [exec_prog_bind_fst, hdecEx, after_decodeInstr] at hstep
    dsimp only [] at hstep
    cases hd' : (exec (dispatchInstr R vid pi dec) ιd T).1 with
    | error err => rw [hd'] at hstep; simp [finalizeStep] at hstep
    | ok pair =>
      rcases pair with ⟨p2, e2⟩
      rw [hd'] at hstep; simp [finalizeStep] at hstep
      rcases hstep with ⟨rfl, rfl⟩
      rfl
  obtain ⟨hActsG, hPcExp⟩ := exec_gatedStep_stepped_actions hrho hpc hdec hdisp
  have hDispEq : exec (dispatchInstr R vid pi dec) ιd T =
      exec (returnRuleProg vid pi dec.rdOps dec.wrOps) ιd T := by
    unfold dispatchInstr
    simp [hop]
  rw [hDispEq] at hdisp hActsG
  unfold returnRuleProg at hdisp hActsG
  match hrdOps : dec.rdOps, hwrOps : dec.wrOps with
  | [], [] =>
    rw [hrdOps, hwrOps] at hdisp hActsG
    simp only [hd, ite_true, Prog.bind_eq, Prog.rd_bind, exec_read] at hdisp hActsG
    cases hvRet : T (.frameRetPc vid pi.depth) with
    | none => rw [hvRet] at hdisp; simp at hdisp
    | some vRet =>
      rw [hvRet] at hdisp hActsG
      cases vRet <;> try (simp at hdisp; done)
      rename_i r
      dsimp only [] at hdisp hActsG
      cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) with
      | false => rw [hrv] at hdisp; simp at hdisp
      | true =>
        rw [hrv] at hdisp hActsG
        simp only [ite_true, Prog.wr_bind, exec_write,
          Prog.pure_eq, exec_ret] at hdisp hActsG
        injection hdisp with hdisp
        injection hdisp with hpi' hevs
        subst hpi' hevs
        simp only [Bool.and_eq_true, decide_eq_true_eq] at hrv
        let pre := [.read (.stopKey vid) none, .read (.pcKey vid) (some (.pc pi))] ++
          (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 ++
          [.read (.frameRetPc vid pi.depth) (some (.pc r))]
        let post : List (Act Key (Val D) I E) :=
          [.write (.delWhere (inFrame vid pi.depth))]
        have hSplit : (exec (gatedStep R vid) ι T).2 =
            pre ++ [.write (.put (.pcKey vid) (.pc r))] ++ post := by
          simpa [pre, post, List.append_assoc] using hActsG
        have hPreW : Act.writes pre = [] := by
          simp [pre, Act.writes_append, writes_decodeInstr_nil]
        refine ⟨pre.length, commitSplit_crash_dichotomy hSplit ?_ hPcExp ?_ hc⟩
        · intro k _; simp [hPreW]
        · intro w hw k haff
          simp only [post, Act.writes_write, Act.writes_nil, List.mem_singleton] at hw
          subst hw
          simp only [Write.affects] at haff
          right; right; right; right
          have hrd : r.depth < pi.depth := by omega
          cases k <;> simp_all [inFrame, isDeadFrameKey]
  | _ :: _, _ => rw [hrdOps] at hdisp; simp at hdisp
  | [], _ :: _ => rw [hrdOps, hwrOps] at hdisp; simp at hdisp

/-- **Mid-`delWhere` continuation simulation for crash-ordered `Return`**:
if `Return` crashes *in the middle* of `delWhere (inFrame vid pi.depth)` after
updating `kappa := r` (leaving an arbitrary subset `deletedMask` of frame
`pi.depth` deleted and the remaining stale keys intact!), the resulting store
`T'` is a valid `CrashAt` state, satisfies `StoreAgree vid T' T_after`, and under
`RuntimeSpec R` and `StepDiscipline vid T_after` executes the *next* `gatedStep`
with the **identical** outcome and events as `T_after`, reaching a store
`StoreAgree`-equivalent to `after (gatedStep R vid) ι₂ T_after`! -/
theorem return_mid_delWhere_next_step {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {pi pi' : PC} {dec : DecodedInstr D} {evs : List E}
    {ι₁ ι₂ : Nat → I} {T : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hop : dec.op = "return")
    (hd : 1 < pi.depth)
    (hstep : (exec (gatedStep R vid) ι₁ T).1 = .stepped pi' evs)
    (deletedMask : Key → Bool) :
    let T_after := after (gatedStep R vid) ι₁ T
    let S_pc := (Write.put (.pcKey vid) (.pc pi')).apply T
    let T_mid : Store Key (Val D) := fun k =>
      if inFrame vid pi.depth k && deletedMask k then none else S_pc k
    (∃ j_mid, CrashAt T (exec (gatedStep R vid) ι₁ T).2 j_mid T_mid) ∧
    StoreAgree vid T_mid T_after ∧
    (StepDiscipline vid T_after →
      (exec (gatedStep R vid) ι₂ T_mid).1 = (exec (gatedStep R vid) ι₂ T_after).1 ∧
      Act.events (exec (gatedStep R vid) ι₂ T_mid).2 =
        Act.events (exec (gatedStep R vid) ι₂ T_after).2 ∧
      StoreAgree vid (after (gatedStep R vid) ι₂ T_mid)
        (after (gatedStep R vid) ι₂ T_after)) := by
  dsimp only []
  let T_after := after (gatedStep R vid) ι₁ T
  let S_pc := (Write.put (.pcKey vid) (.pc pi')).apply T
  let T_mid : Store Key (Val D) := fun k =>
    if inFrame vid pi.depth k && deletedMask k then none else S_pc k
  -- Extract exact action list of `Return` to exhibit `j_mid = as.length - 1`:
  let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι₁ T
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι₁ T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hdisp : (exec (dispatchInstr R vid pi dec) ιd T).1 = .ok (pi', evs) := by
    unfold gatedStep at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho] at hstep
    rw [exec_prog_bind_fst] at hstep
    unfold stepCore at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read] at hstep
    rw [exec_prog_bind_fst, hdecEx, after_decodeInstr] at hstep
    dsimp only [] at hstep
    cases hd' : (exec (dispatchInstr R vid pi dec) ιd T).1 with
    | error err => rw [hd'] at hstep; simp [finalizeStep] at hstep
    | ok pair =>
      rcases pair with ⟨p2, e2⟩
      rw [hd'] at hstep; simp [finalizeStep] at hstep
      rcases hstep with ⟨rfl, rfl⟩
      rfl
  obtain ⟨hActsG, _⟩ := exec_gatedStep_stepped_actions hrho hpc hdec hdisp
  have hDispEq : exec (dispatchInstr R vid pi dec) ιd T =
      exec (returnRuleProg vid pi dec.rdOps dec.wrOps) ιd T := by
    unfold dispatchInstr; simp [hop]
  rw [hDispEq] at hdisp hActsG
  unfold returnRuleProg at hdisp hActsG
  match hrdOps : dec.rdOps, hwrOps : dec.wrOps with
  | [], [] =>
    rw [hrdOps, hwrOps] at hdisp hActsG
    simp only [hd, ite_true, Prog.bind_eq, Prog.rd_bind, exec_read] at hdisp hActsG
    cases hvRet : T (.frameRetPc vid pi.depth) with
    | none => rw [hvRet] at hdisp; simp at hdisp
    | some vRet =>
      rw [hvRet] at hdisp hActsG
      cases vRet <;> try (simp at hdisp; done)
      rename_i r
      dsimp only [] at hdisp hActsG
      cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) with
      | false => rw [hrv] at hdisp; simp at hdisp
      | true =>
        rw [hrv] at hdisp hActsG
        simp only [ite_true, Prog.wr_bind, exec_write,
          Prog.pure_eq, exec_ret] at hdisp hActsG
        injection hdisp with hdisp
        injection hdisp with hpi' hevs
        subst hpi' hevs
        let pre1 := [.read (.stopKey vid) none, .read (.pcKey vid) (some (.pc pi))] ++
          (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι₁ T).2 ++
          [.read (.frameRetPc vid pi.depth) (some (.pc r)),
           .write (.put (.pcKey vid) (.pc r))]
        have hActsEq : (exec (gatedStep R vid) ι₁ T).2 =
            pre1 ++ [.write (.delWhere (inFrame vid pi.depth))] := by
          simpa [pre1, List.append_assoc] using hActsG
        have hPre1W : Act.writes pre1 = [.put (.pcKey vid) (.pc r)] := by
          simp [pre1, Act.writes_append, writes_decodeInstr_nil]
        have hcMid : CrashAt T (exec (gatedStep R vid) ι₁ T).2 pre1.length T_mid := by
          intro k
          rw [hActsEq, List.take_left, List.drop_left, hPre1W]
          simp only [applyAll_cons, applyAll_nil, headPartialKeys, Act.partialKeys]
          cases hMask : (inFrame vid pi.depth k && deletedMask k) with
          | false =>
            left
            dsimp only [T_mid, S_pc]
            rw [hMask]
            rfl
          | true =>
            right
            have hFrame : inFrame vid pi.depth k = true := by
              cases hIn : inFrame vid pi.depth k
              · simp [hIn] at hMask
              · rfl
            refine ⟨hFrame, ?_⟩
            dsimp only [T_mid]
            rw [hMask]
            rfl
        obtain ⟨j_commit, hPreBranch | hPostBranch⟩ :=
          return_gatedStep_crash_dichotomy hrho hpc hdec hop hd hstep hcMid
        · -- Impossible: `T_mid (.pcKey vid) = some (.pc r) ≠ some (.pc pi)` since `r.depth + 1 = pi.depth`
          exfalso
          simp only [Bool.and_eq_true, decide_eq_true_eq] at hrv
          have hMidPc : T_mid (.pcKey vid) = some (.pc r) := by
            simp [T_mid, S_pc, inFrame, Write.apply, Write.affects, Write.newVal]
          have hEqPc := hMidPc.symm.trans (hPreBranch.2.2.1.trans hpc)
          injection hEqPc with hEqPi
          injection hEqPi with hEqR
          have : r.depth = pi.depth := congrArg PC.depth hEqR
          omega
        · have hAgreeMid : StoreAgree vid T_mid T_after := hPostBranch.2.2
          refine ⟨⟨pre1.length, hcMid⟩, hAgreeMid, ?_⟩
          intro hPAfter
          have hMidRho : T_mid (.stopKey vid) = none := by
            simp [T_mid, S_pc, inFrame, Write.apply, Write.affects, hrho]
          have hAfterRho : T_after (.stopKey vid) = none := by
            unfold T_after after
            rw [hActsEq, Act.writes_append, hPre1W]
            simp [Write.apply, Write.affects, inFrame, hrho]
          obtain ⟨hExecEq, hAg2⟩ :=
            step_respects_agree hR hAgreeMid.symm hAfterRho hMidRho hPAfter ι₂
          exact ⟨congrArg Prod.fst hExecEq, congrArg (Act.events ∘ Prod.snd) hExecEq, hAg2.symm⟩
  | _ :: _, _ => rw [hrdOps] at hdisp; simp at hdisp
  | [], _ :: _ => rw [hrdOps, hwrOps] at hdisp; simp at hdisp

/-- **Global `StepWarFree` for root `Return` (`¬ (1 < pi.depth)`)**:
at root frame depth `1`, `Return` writes only `.put (.statusKey vid) (.status .ok)`
and never reads `.statusKey vid`, so `StepWarFree` holds across *all* cuts `j`. -/
theorem returnRoot_stepWarFree {R : Runtime D I E} {vid : Vid}
    {pi pi' : PC} {dec : DecodedInstr D} {evs : List E}
    {ι : Nat → I} {T : Store Key (Val D)}
    (hrho : T (.stopKey vid) = none)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hop : dec.op = "return")
    (hd : ¬ (1 < pi.depth))
    (hstep : (exec (gatedStep R vid) ι T).1 = .stepped pi' evs) :
    StepWarFree (gatedStep R vid) ι T := by
  let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
  have hdecEx : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 = .ok dec := by
    rw [exec_decodeInstr_fst]; exact hdec
  have hdisp : (exec (dispatchInstr R vid pi dec) ιd T).1 = .ok (pi', evs) := by
    unfold gatedStep at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrho] at hstep
    rw [exec_prog_bind_fst] at hstep
    unfold stepCore at hstep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc, after_read] at hstep
    rw [exec_prog_bind_fst, hdecEx, after_decodeInstr] at hstep
    dsimp only [] at hstep
    cases hd' : (exec (dispatchInstr R vid pi dec) ιd T).1 with
    | error err => rw [hd'] at hstep; simp [finalizeStep] at hstep
    | ok pair =>
      rcases pair with ⟨p2, e2⟩
      rw [hd'] at hstep; simp [finalizeStep] at hstep
      rcases hstep with ⟨rfl, rfl⟩
      rfl
  obtain ⟨hActsG, _⟩ := exec_gatedStep_stepped_actions hrho hpc hdec hdisp
  have hDispEq : exec (dispatchInstr R vid pi dec) ιd T =
      exec (returnRuleProg vid pi dec.rdOps dec.wrOps) ιd T := by
    unfold dispatchInstr; simp [hop]
  rw [hDispEq] at hdisp hActsG
  unfold returnRuleProg at hdisp hActsG
  match hrdOps : dec.rdOps, hwrOps : dec.wrOps with
  | [], [] =>
    rw [hrdOps, hwrOps] at hdisp hActsG
    simp only [hd, ite_false, Prog.bind_eq, Prog.wr_bind, exec_write,
      Prog.pure_eq, exec_ret] at hActsG
    apply StepWarFree.of_noReadWriteOverlap
    intro k ⟨v, hm⟩
    rw [hActsG] at hm ⊢
    simp only [Act.writes_append, Act.writes_read, Act.writes_write, Act.writes_nil,
      writes_decodeInstr_nil, List.nil_append, touches_cons, touches_nil,
      Write.affects, Bool.or_false, decide_eq_false_iff_not]
    simp only [List.mem_append, List.mem_cons, List.not_mem_nil, or_false] at hm
    rcases hm with ((hEq | hEq) | hmDec) | hEq
    · cases hEq; intro h; cases h
    · cases hEq; intro h; cases h
    · rcases decodeInstr_reads_kind hmDec with rfl | hCode
      · intro h; cases h
      · rintro rfl; cases hCode
    · cases hEq
  | _ :: _, _ => rw [hrdOps] at hdisp; simp at hdisp
  | [], _ :: _ => rw [hrdOps, hwrOps] at hdisp; simp at hdisp

/-- Hypothetical *reversed-order* `Return` step program that runs
`delWhere (inFrame vid pi.depth)` *before* updating `kappa := r` (§4, Theorem 4). -/
def reversedReturnStep (vid : Vid) : Prog Key (Val D) I E (Except ErrMsg PC) := do
  let vPc ← Prog.rd (.pcKey vid)
  match vPc with
  | some (.pc pi) =>
      if 1 < pi.depth then do
        let vRet ← Prog.rd (.frameRetPc vid pi.depth)
        match vRet with
        | some (.pc r) =>
            if r.valid && decide (r.depth + 1 = pi.depth) then do
              Prog.wr (.delWhere (inFrame vid pi.depth))
              Prog.wr (.put (.pcKey vid) (.pc r))
              pure (.ok r)
            else pure (.error "broken_return_chain")
        | _ => pure (.error "missing_returnpc")
      else pure (.error "root")
  | _ => pure (.error "missing_pc")

/-- **Counterexample for reversed-order `Return`**: deleting frame `pi.depth`
*before* writing `kappa := r` deletes `[pi.depth]..returnpc` while `kappa` still
holds `pi`. A crash after `delWhere` (or mid-`delWhere` once `..returnpc` is
removed) violates `WarFreeAt`, leaves `T'(kappa) = T(kappa) = some (.pc pi)`,
and causes replay from `T'` to fail with `.error "missing_returnpc"`. -/
theorem reversed_return_crash_counterexample :
    ∃ (vid : Vid) (T T' : Store Key (Val Unit)) (j : Nat),
      (exec (reversedReturnStep (I := Unit) (E := Unit) vid) (fun _ => ()) T).1 = .ok ⟨1, 2⟩ ∧
      CrashAt T (exec (reversedReturnStep (I := Unit) (E := Unit) vid) (fun _ => ()) T).2 j T' ∧
      T' (.pcKey vid) = T (.pcKey vid) ∧
      ¬ WarFreeAt (exec (reversedReturnStep (I := Unit) (E := Unit) vid) (fun _ => ()) T).2 j ∧
      (exec (reversedReturnStep (I := Unit) (E := Unit) vid) (fun _ => ()) T').1 =
        .error "missing_returnpc" := by
  let vid : Vid := 1
  let T : Store Key (Val Unit) := fun k =>
    if k = .pcKey vid then some (.pc ⟨2, 1⟩)
    else if k = .frameRetPc vid 2 then some (.pc ⟨1, 2⟩)
    else none
  let as := (exec (reversedReturnStep (D := Unit) (I := Unit) (E := Unit) vid) (fun _ => ()) T).2
  let T' : Store Key (Val Unit) := applyAll T (Act.writes (as.take 3))
  refine ⟨vid, T, T', 3, rfl, crashAt_prefix T as 3, rfl, ?_, rfl⟩
  intro hwf
  have hr := hwf (.frameRetPc vid 2) rfl
  exact absurd hr (by decide)

end ConcreteRules

end Pathlang
