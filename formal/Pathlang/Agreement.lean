import Pathlang.Footprint

/-!
# Store Agreement, Lease Mutual Exclusion, and Theorem 3 (T3)

1. Store agreement relation `StoreAgree` (`~` in `SPEC-v2.md`), ignoring
   `sigma`, `mu`, `rho`, `lambda`, and dead frames above live depth.
2. Conditional write `createIfAbsent` and lease mutual exclusion on `lambda`
   (`lease_mutual_exclusion`).
3. Lemma 8 (`lem:approx`, `step_respects_agree`): gated steps from agreeing
   runnable stores perform identical actions and reach agreeing stores under
   program discipline `(P)`.
4. Theorem 3 (`thm:resumption`, `stop_resume_resumption`, `stop_resume_isPrefix`,
   `stop_resume_completed`): any action-level or step-boundary run with arbitrary
   stops, lease acquisitions/releases, and resumes produces a prefix of the
   uninterrupted trace and ends in a store agreeing with the corresponding
   commit point, preserving terminal `ok`.
-/

namespace Pathlang

/-! ## Store Agreement (`~`) -/

section StoreAgreeBasic

variable {D : Type}

/-- Live frame depth determined by `T(kappa)` (`0` if `kappa` holds no counter). -/
def liveDepthOf (vid : Vid) (T : Store Key (Val D)) : Nat :=
  match T (.pcKey vid) with
  | some (.pc pi) => pi.depth
  | _ => 0

/-- Keys ignored by v2 store agreement at live depth `d`:
`sigma`, `mu`, `rho`, `lambda`, and dead frames (`e = 0` or `e > d`). -/
def IgnoredByAgree (vid : Vid) (d : Nat) (k : Key) : Prop :=
  k = .statusKey vid ∨ k = .msgKey vid ∨ k = .stopKey vid ∨
  k = .driverKey vid ∨ isDeadFrameKey vid d k = true

/-- **Store Agreement (`T ~ U`, line 89)**:
`T` and `U` hold the same counter at `kappa` and agree at every key outside
`sigma`, `mu`, `rho`, `lambda`, and frames dead under `liveDepthOf vid T`. -/
def StoreAgree (vid : Vid) (T U : Store Key (Val D)) : Prop :=
  T (.pcKey vid) = U (.pcKey vid) ∧
  ∀ k, ¬ IgnoredByAgree vid (liveDepthOf vid T) k → T k = U k

theorem liveDepthOf_eq_of_pc_eq {vid : Vid} {T U : Store Key (Val D)}
    (hpc : T (.pcKey vid) = U (.pcKey vid)) :
    liveDepthOf vid T = liveDepthOf vid U := by
  simp [liveDepthOf, hpc]

@[simp] theorem liveDepthOf_pc {vid : Vid} {T : Store Key (Val D)} {pi : PC}
    (hpc : T (.pcKey vid) = some (.pc pi)) :
    liveDepthOf vid T = pi.depth := by
  simp [liveDepthOf, hpc]

theorem pcKey_not_ignored (vid : Vid) (d : Nat) :
    ¬ IgnoredByAgree vid d (.pcKey vid) := by
  rintro (h | h | h | h | h) <;> cases h

theorem StoreAgree.refl (vid : Vid) (T : Store Key (Val D)) :
    StoreAgree vid T T :=
  ⟨rfl, fun _ _ => rfl⟩

theorem StoreAgree.symm {vid : Vid} {T U : Store Key (Val D)}
    (h : StoreAgree vid T U) : StoreAgree vid U T := by
  refine ⟨h.1.symm, ?_⟩
  intro k hk
  rw [← liveDepthOf_eq_of_pc_eq h.1] at hk
  exact (h.2 k hk).symm

theorem StoreAgree.trans {vid : Vid} {T U W : Store Key (Val D)}
    (h₁ : StoreAgree vid T U) (h₂ : StoreAgree vid U W) :
    StoreAgree vid T W := by
  refine ⟨h₁.1.trans h₂.1, ?_⟩
  intro k hk
  have hkU : ¬ IgnoredByAgree vid (liveDepthOf vid U) k := by
    rw [← liveDepthOf_eq_of_pc_eq h₁.1]
    exact hk
  exact (h₁.2 k hk).trans (h₂.2 k hkU)

theorem forbidden_of_ignored {vid : Vid} {d : Nat} {k : Key}
    (h : IgnoredByAgree vid d k) : ForbiddenKey vid d k = true := by
  rcases h with rfl | rfl | rfl | rfl | hdead
  · simp [ForbiddenKey, isThreadControlKey]
  · simp [ForbiddenKey, isThreadControlKey]
  · simp [ForbiddenKey, isThreadControlKey]
  · simp [ForbiddenKey, isThreadControlKey]
  · simp [ForbiddenKey, hdead]

theorem not_ignored_of_not_forbidden {vid : Vid} {d : Nat} {k : Key}
    (h : ForbiddenKey vid d k = false) : ¬ IgnoredByAgree vid d k := by
  intro hIg
  rw [forbidden_of_ignored hIg] at h
  cases h

theorem storeAgree_apply_ignored {vid : Vid} {w : Write Key (Val D)}
    {T : Store Key (Val D)}
    (hpc : w.affects (.pcKey vid) = false)
    (hw : ∀ k, w.affects k = true → IgnoredByAgree vid (liveDepthOf vid T) k) :
    StoreAgree vid (w.apply T) T := by
  have hpcEq : w.apply T (.pcKey vid) = T (.pcKey vid) :=
    Write.apply_of_not_affects T hpc
  refine ⟨hpcEq, ?_⟩
  intro k hk
  rw [liveDepthOf_eq_of_pc_eq hpcEq] at hk
  cases haff : w.affects k with
  | false => exact Write.apply_of_not_affects T haff
  | true => exact absurd (hw k haff) hk

theorem storeAgree_stopWrite (vid : Vid) (T : Store Key (Val D)) :
    StoreAgree vid ((stopWrite vid).apply T) T := by
  apply storeAgree_apply_ignored rfl
  intro k hk
  simp only [stopWrite, Write.affects, decide_eq_true_eq] at hk
  subst hk
  exact Or.inr (Or.inr (Or.inl rfl))

theorem storeAgree_applyStops (vid : Vid) (n : Nat) (T : Store Key (Val D)) :
    StoreAgree vid (applyStops vid n T) T := by
  induction n with
  | zero => exact StoreAgree.refl vid T
  | succ n ih =>
    exact (storeAgree_stopWrite vid (applyStops vid n T)).trans ih

theorem storeAgree_delRho (vid : Vid) (T : Store Key (Val D)) :
    StoreAgree vid ((Write.del (.stopKey vid)).apply T) T := by
  apply storeAgree_apply_ignored rfl
  intro k hk
  simp only [Write.affects, decide_eq_true_eq] at hk
  subst hk
  exact Or.inr (Or.inr (Or.inl rfl))

theorem storeAgree_putStatus (vid : Vid) (s : Status) (T : Store Key (Val D)) :
    StoreAgree vid ((Write.put (.statusKey vid) (.status s)).apply T) T := by
  apply storeAgree_apply_ignored rfl
  intro k hk
  simp only [Write.affects, decide_eq_true_eq] at hk
  subst hk
  exact Or.inl rfl

theorem storeAgree_putMsg (vid : Vid) (err : ErrMsg) (T : Store Key (Val D)) :
    StoreAgree vid ((Write.put (.msgKey vid) (.errMsg err)).apply T) T := by
  apply storeAgree_apply_ignored rfl
  intro k hk
  simp only [Write.affects, decide_eq_true_eq] at hk
  subst hk
  exact Or.inr (Or.inl rfl)

theorem storeAgree_leasePut (vid : Vid) (tok : Token) (T : Store Key (Val D)) :
    StoreAgree vid ((Write.put (.driverKey vid) (.lease tok)).apply T) T := by
  apply storeAgree_apply_ignored rfl
  intro k hk
  simp only [Write.affects, decide_eq_true_eq] at hk
  subst hk
  exact Or.inr (Or.inr (Or.inr (Or.inl rfl)))

theorem storeAgree_leaseDel (vid : Vid) (T : Store Key (Val D)) :
    StoreAgree vid ((Write.del (.driverKey vid)).apply T) T := by
  apply storeAgree_apply_ignored rfl
  intro k hk
  simp only [Write.affects, decide_eq_true_eq] at hk
  subst hk
  exact Or.inr (Or.inr (Or.inr (Or.inl rfl)))

theorem storeAgree_resumeStore (vid : Vid) (T : Store Key (Val D)) :
    StoreAgree vid (resumeStore vid T) T := by
  unfold resumeStore
  simp only [applyAll_cons, applyAll_nil]
  exact (storeAgree_putStatus vid .running _).trans (storeAgree_delRho vid T)

theorem storeAgree_pauseAfterGate {I E : Type} (vid : Vid)
    (ι : Nat → I) (T : Store Key (Val D)) :
    StoreAgree vid (after (pauseAfterGate (D := D) (I := I) (E := E) vid) ι T) T := by
  simpa [pauseAfterGate, after] using storeAgree_putStatus vid .paused T

end StoreAgreeBasic

/-! ## Conditional Write `createIfAbsent` and Lease Mutual Exclusion -/

section LeaseMutex

/-- Atomic conditional create (Store API `pathspaceCreateIfAbsent`):
writes `v` at `k` iff `k` is currently absent, returning whether it created `k`. -/
def createIfAbsent {K V : Type} [DecidableEq K] (k : K) (v : V) (T : Store K V) :
    Bool × Store K V :=
  match T k with
  | none => (true, (Write.put k v).apply T)
  | some _ => (false, T)

@[simp] theorem createIfAbsent_absent {K V : Type} [DecidableEq K]
    {k : K} (v : V) {T : Store K V} (h : T k = none) :
    createIfAbsent k v T = (true, (Write.put k v).apply T) := by
  simp [createIfAbsent, h]

@[simp] theorem createIfAbsent_present {K V : Type} [DecidableEq K]
    {k : K} (v w : V) {T : Store K V} (h : T k = some w) :
    createIfAbsent k v T = (false, T) := by
  simp [createIfAbsent, h]

/-- Sequentialized outcomes of `k` contending processes attempting to acquire
`lambda = /vthread/{vid}/..driver` via atomic `createIfAbsent`. -/
def tryAcquireAll {D : Type} (vid : Vid) :
    List Token → Store Key (Val D) → List Bool × Store Key (Val D)
  | [], T => ([], T)
  | tok :: toks, T =>
      let r₁ := createIfAbsent (.driverKey vid) (.lease tok) T
      let r₂ := tryAcquireAll vid toks r₁.2
      (r₁.1 :: r₂.1, r₂.2)

theorem lease_busy_rejects_all {D : Type} (vid : Vid) (toks : List Token)
    {T : Store Key (Val D)} {v : Val D} (hHeld : T (.driverKey vid) = some v) :
    tryAcquireAll vid toks T = (toks.map (fun _ => false), T) := by
  induction toks with
  | nil => rfl
  | cons tok toks ih =>
    simp [tryAcquireAll, hHeld, ih]

/-- **Lease Mutual Exclusion (`(S) (v2)` and Q1 Part E)**:
when `lambda` is initially absent, among any non-empty sequence `tok :: toks` of
contenders calling `createIfAbsent` on `lambda`, the first contender `tok`
succeeds (`true`) and holds `lambda = lease tok`, every subsequent contender
receives `false` without mutating the store, and the resulting store agrees with
`T` under `StoreAgree`. -/
theorem lease_mutual_exclusion {D : Type} (vid : Vid) (tok : Token) (toks : List Token)
    {T : Store Key (Val D)} (hFree : T (.driverKey vid) = none) :
    let res := tryAcquireAll vid (tok :: toks) T
    res.1 = true :: toks.map (fun _ => false) ∧
    res.2 = (Write.put (.driverKey vid) (.lease tok)).apply T ∧
    res.2 (.driverKey vid) = some (.lease tok) ∧
    StoreAgree vid res.2 T := by
  let T₁ := (Write.put (K := Key) (V := Val D) (.driverKey vid) (.lease tok)).apply T
  have hHeld : T₁ (.driverKey vid) = some (.lease tok) := by
    simp [T₁, Write.apply, Write.affects, Write.newVal]
  have hRest := lease_busy_rejects_all vid toks hHeld
  dsimp only [tryAcquireAll]
  rw [createIfAbsent_absent _ hFree]
  dsimp only []
  rw [hRest]
  exact ⟨rfl, rfl, hHeld, storeAgree_leasePut vid tok T⟩

end LeaseMutex

/-! ## Program Discipline `(P)` and Lemma 8 (`lem:approx`, `step_respects_agree`) -/

section StepRespectsAgree

variable {D I E : Type}

/-- General read-exposure consequence of `ExecAvoids`: if no action of `P`
reads a `badRd` key, then no `badRd` key is exposed by `P`. -/
theorem ExecAvoids.not_exposed {K V : Type} [DecidableEq K] {α : Type}
    {badRd badWr : K → Prop} {P : Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (h : ExecAvoids badRd badWr P ι T) {k : K}
    (hexp : Act.exposed (exec P ι T).2 k = true) : ¬ badRd k := by
  obtain ⟨v, hv⟩ := Act.reads_of_exposed hexp
  exact h.1 k v hv

/-- On any key touched by `ws`, `applyAll` produces the exact same final value
from any two starting stores `T₁` and `T₂`. -/
theorem applyAll_eq_of_touches {K V : Type} [DecidableEq K]
    (T₁ T₂ : Store K V) {ws : List (Write K V)} {k : K}
    (h : touches ws k = true) :
    applyAll T₁ ws k = applyAll T₂ ws k := by
  induction ws generalizing T₁ T₂ with
  | nil => simp at h
  | cons w ws ih =>
    simp only [touches_cons, Bool.or_eq_true] at h
    rw [applyAll_cons, applyAll_cons]
    cases htail : touches ws k with
    | true => exact ih (w.apply T₁) (w.apply T₂) htail
    | false =>
      have hw : w.affects k = true := by
        rcases h with hw | ht
        · exact hw
        · rw [htail] at ht; cases ht
      rw [applyAll_of_not_touches htail, applyAll_of_not_touches htail,
          Write.apply_of_affects T₁ hw, Write.apply_of_affects T₂ hw]

theorem readOperandsPure_congr {fn : SymName} {row : Nat} {dir : ParamDir}
    {T U : Store Key (Val D)}
    (hLib : ∀ idx, T (.libOperand fn row dir idx) = U (.libOperand fn row dir idx)) :
    ∀ idx count, readOperandsPure fn row dir T idx count =
      readOperandsPure fn row dir U idx count := by
  intro idx count
  induction count generalizing idx with
  | zero => rfl
  | succ count ih =>
    simp only [readOperandsPure, hLib idx, ih (idx + 1)]

theorem decodeInstrPure_congr {vid : Vid} {pi : PC} {T U : Store Key (Val D)}
    (hAgree : StoreAgree vid T U)
    (hpc : T (.pcKey vid) = some (.pc pi)) :
    decodeInstrPure vid pi T = decodeInstrPure vid pi U := by
  have hd : liveDepthOf vid T = pi.depth := liveDepthOf_pc hpc
  unfold decodeInstrPure
  cases hpv : pi.valid with
  | false => rfl
  | true =>
    have hge : 1 ≤ pi.depth := (PC.valid_iff pi).1 hpv |>.1
    have hLibNotIg : ¬ IgnoredByAgree vid (liveDepthOf vid T) (.frameLib vid pi.depth) := by
      rw [hd]
      rintro (h | h | h | h | hdead) <;> try cases h
      simp only [isDeadFrameKey, decide_eq_true_eq] at hdead
      omega
    rw [hAgree.2 _ hLibNotIg]
    cases U (.frameLib vid pi.depth) with
    | none => rfl
    | some vLib =>
      cases vLib <;> try rfl
      rename_i fn
      dsimp only []
      have hInsNotIg : ¬ IgnoredByAgree vid (liveDepthOf vid T) (.libInstr fn pi.row) := by
        rintro (h | h | h | h | h) <;> cases h
      have hOpEq : ∀ dir idx, T (.libOperand fn pi.row dir idx) =
          U (.libOperand fn pi.row dir idx) := by
        intro dir idx
        apply hAgree.2
        rintro (h | h | h | h | h) <;> cases h
      rw [hAgree.2 _ hInsNotIg]
      cases U (.libInstr fn pi.row) with
      | none => rfl
      | some vIns =>
        cases vIns <;> try rfl
        rename_i op nr nw
        dsimp only []
        rw [readOperandsPure_congr (hOpEq .rd) 1 nr,
            readOperandsPure_congr (hOpEq .wr) 1 nw]

/-- Program discipline `(P)` (`StepDiscipline`) respects `StoreAgree`. -/
theorem StepDiscipline.of_storeAgree {vid : Vid} {T U : Store Key (Val D)}
    (hAgree : StoreAgree vid T U) (hP : StepDiscipline vid T) :
    StepDiscipline vid U := by
  intro pi dec hpcU hdecU
  have hpcT : T (.pcKey vid) = some (.pc pi) := hAgree.1.trans hpcU
  have hd : liveDepthOf vid T = pi.depth := liveDepthOf_pc hpcT
  have hdecT : decodeInstrPure vid pi T = .ok dec :=
    (decodeInstrPure_congr hAgree hpcT).trans hdecU
  obtain ⟨hrd, hwr⟩ := hP pi dec hpcT hdecT
  have hDiff : ∀ r, ForbiddenKey vid pi.depth r = false → U r = T r := by
    intro r hr
    apply (hAgree.2 r ?_).symm
    rw [hd]
    exact not_ignored_of_not_forbidden hr
  exact ⟨fun x hx => readOpSafe_of_agree_nonforbidden hDiff (hrd x hx),
         fun y hy => writeOpSafe_of_agree_nonforbidden hDiff (hwr y hy)⟩

/-! ### Read-Footprint Avoidance of `IgnoredByAgree` -/

private abbrev NoBadWr : Key → Prop := fun _ => False

private theorem no_bad_wr (w : Write Key (Val D)) :
    ∀ k, w.affects k = true → ¬ NoBadWr k :=
  fun _ _ h => h

private theorem resolveRead_reads_live {vid : Vid} {pi : PC} {x : Key × Val D}
    {T : Store Key (Val D)} (hx : ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (resolveRead (I := I) (E := E) vid pi x) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hmem
    rw [exec_resolveRead] at hmem
    simp only [List.mem_map] at hmem
    obtain ⟨r, hr, hact⟩ := hmem
    cases hact
    exact not_ignored_of_not_forbidden (hx.1 k hr)
  · intro w hmem
    rw [exec_resolveRead] at hmem
    simp at hmem

private theorem resolveWrite_reads_live {vid : Vid} {pi : PC} {y : Key × Val D}
    {T : Store Key (Val D)} (hy : WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (resolveWrite (I := I) (E := E) vid pi y) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hmem
    rw [exec_resolveWrite] at hmem
    simp only [List.mem_map] at hmem
    obtain ⟨r, hr, hact⟩ := hmem
    cases hact
    exact not_ignored_of_not_forbidden (hy.1 k hr)
  · intro w hmem
    rw [exec_resolveWrite] at hmem
    simp at hmem

private theorem resolveAllRead_reads_live {vid : Vid} {pi : PC}
    {xs : List (Key × Val D)} {T : Store Key (Val D)}
    (hxs : ∀ x ∈ xs, ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (resolveAllRead (I := I) (E := E) vid pi xs) ι T := by
  induction xs generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons x xs ih =>
    unfold resolveAllRead
    apply ExecAvoids.bind_op (resolveRead_reads_live (hxs x List.mem_cons_self) ι)
    rw [after_resolveRead]
    cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some _ =>
      apply ExecAvoids.bind_op
        (ih (fun z hz => hxs z (List.mem_cons_of_mem _ hz)) _)
      cases (exec (resolveAllRead (I := I) (E := E) vid pi xs) _ T).1 <;>
        exact ExecAvoids.ret _ _ _

private theorem resolveAllWrite_reads_live {vid : Vid} {pi : PC}
    {ys : List (Key × Val D)} {T : Store Key (Val D)}
    (hys : ∀ y ∈ ys, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (resolveAllWrite (I := I) (E := E) vid pi ys) ι T := by
  induction ys generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons y ys ih =>
    unfold resolveAllWrite
    apply ExecAvoids.bind_op (resolveWrite_reads_live (hys y List.mem_cons_self) ι)
    rw [after_resolveWrite]
    cases (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some _ =>
      apply ExecAvoids.bind_op
        (ih (fun z hz => hys z (List.mem_cons_of_mem _ hz)) _)
      cases (exec (resolveAllWrite (I := I) (E := E) vid pi ys) _ T).1 <;>
        exact ExecAvoids.ret _ _ _

private theorem resolveRowTarget_reads_live {vid : Vid} {pi : PC} {x : Key × Val D}
    {T : Store Key (Val D)} (hx : ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (resolveRowTarget (I := I) (E := E) vid pi x) ι T := by
  unfold resolveRowTarget
  apply ExecAvoids.bind_op (resolveRead_reads_live hx ι)
  rw [after_resolveRead]
  cases hp : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => exact ExecAvoids.ret _ _ _
  | some p =>
    have hp' := hp; rw [exec_resolveRead] at hp'
    apply ExecAvoids.rd_bind (not_ignored_of_not_forbidden (hx.2 p hp'))
    cases T p with
    | none => exact ExecAvoids.ret _ _ _
    | some v =>
      cases v <;> try exact ExecAvoids.ret _ _ _
      rename_i z
      cases z with
      | ofNat m => cases m <;> exact ExecAvoids.ret _ _ _
      | negSucc _ => exact ExecAvoids.ret _ _ _

private theorem resolveTruth_reads_live {vid : Vid} {pi : PC} {x : Key × Val D}
    {T : Store Key (Val D)} (hx : ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (resolveTruth (I := I) (E := E) vid pi x) ι T := by
  unfold resolveTruth
  apply ExecAvoids.bind_op (resolveRead_reads_live hx ι)
  rw [after_resolveRead]
  cases hp : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => exact ExecAvoids.ret _ _ _
  | some p =>
    have hp' := hp; rw [exec_resolveRead] at hp'
    apply ExecAvoids.rd_bind (not_ignored_of_not_forbidden (hx.2 p hp'))
    cases T p with
    | none => exact ExecAvoids.ret _ _ _
    | some v => cases v <;> exact ExecAvoids.ret _ _ _

private theorem readOperandsFrom_reads_live (vid : Vid) (d : Nat) (fn : SymName)
    (row : Nat) (dir : ParamDir) (idx count : Nat) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IgnoredByAgree vid d) NoBadWr
      (readOperandsFrom (D := D) (I := I) (E := E) fn row dir idx count) ι T := by
  induction count generalizing idx ι with
  | zero => exact ExecAvoids.ret _ _ _
  | succ count ih =>
    unfold readOperandsFrom
    have hk : ¬ IgnoredByAgree vid d (.libOperand fn row dir idx) := by
      rintro (h | h | h | h | h) <;> cases h
    apply ExecAvoids.rd_bind hk
    cases T (.libOperand fn row dir idx) with
    | none => exact ExecAvoids.ret _ _ _
    | some _ =>
      apply ExecAvoids.bind_op (ih (idx + 1) _)
      cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn row dir (idx + 1) count) _ _).1 <;>
        exact ExecAvoids.ret _ _ _

private theorem decodeInstr_reads_live (vid : Vid) (pi : PC)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T := by
  unfold decodeInstr
  cases hpv : pi.valid with
  | false =>
    simp only [Bool.not_false, ite_true]
    exact ExecAvoids.ret _ _ _
  | true =>
    simp only [Bool.not_true, Bool.false_eq_true, ite_false]
    have hge : 1 ≤ pi.depth := (PC.valid_iff pi).1 hpv |>.1
    have hLib : ¬ IgnoredByAgree vid pi.depth (.frameLib vid pi.depth) := by
      rintro (h | h | h | h | hdead) <;> try cases h
      simp only [isDeadFrameKey, decide_eq_true_eq] at hdead
      omega
    apply ExecAvoids.rd_bind hLib
    cases T (.frameLib vid pi.depth) with
    | none => exact ExecAvoids.ret _ _ _
    | some vLib =>
      cases vLib <;> try exact ExecAvoids.ret _ _ _
      rename_i fn
      have hIns : ¬ IgnoredByAgree vid pi.depth (.libInstr fn pi.row) := by
        rintro (h | h | h | h | h) <;> cases h
      apply ExecAvoids.rd_bind hIns
      cases T (.libInstr fn pi.row) with
      | none => exact ExecAvoids.ret _ _ _
      | some vIns =>
        cases vIns <;> try exact ExecAvoids.ret _ _ _
        rename_i op nr nw
        apply ExecAvoids.bind_op (readOperandsFrom_reads_live vid pi.depth fn pi.row .rd 1 nr _ _)
        cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .rd 1 nr) _ _).1 with
        | none => exact ExecAvoids.ret _ _ _
        | some _ =>
          apply ExecAvoids.bind_op (readOperandsFrom_reads_live vid pi.depth fn pi.row .wr 1 nw _ _)
          cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .wr 1 nw) _ _).1 <;>
            exact ExecAvoids.ret _ _ _

private theorem bindReadArgs_reads_live {vid : Vid} {pi : PC} (idx : Nat)
    (xs : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D))
    (hxs : ∀ x ∈ xs, ReadOpSafe vid pi T x) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (bindReadArgs (I := I) (E := E) vid pi idx xs) ι T := by
  induction xs generalizing idx ι T with
  | nil => exact ExecAvoids.ret _ _ _
  | cons x xs ih =>
    have hx := hxs x List.mem_cons_self
    unfold bindReadArgs
    apply ExecAvoids.bind_op (resolveRead_reads_live hx ι)
    rw [after_resolveRead]
    cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some p =>
      let w := Write.put (K := Key) (V := Val D) (.frameParam vid (pi.depth + 1) .rd idx) (.ptr p)
      have hwChild : ∀ k, w.affects k = true → inFrame vid (pi.depth + 1) k = true := by
        intro k hk; simp only [w, Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame]
      have hT' := write_child_preserves_nonforbidden (T := T) hwChild
      have hxs' : ∀ z ∈ xs, ReadOpSafe vid pi (w.apply T) z :=
        fun z hz => readOpSafe_of_agree_nonforbidden hT' (hxs z (List.mem_cons_of_mem _ hz))
      exact ExecAvoids.wr_bind (no_bad_wr w) (ih (idx + 1) _ (w.apply T) hxs')

private theorem bindWriteArgs_reads_live {vid : Vid} {pi : PC} (idx : Nat)
    (ys : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D))
    (hys : ∀ y ∈ ys, WriteOpSafe vid pi T y) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (bindWriteArgs (I := I) (E := E) vid pi idx ys) ι T := by
  induction ys generalizing idx ι T with
  | nil => exact ExecAvoids.ret _ _ _
  | cons y ys ih =>
    have hy := hys y List.mem_cons_self
    unfold bindWriteArgs
    apply ExecAvoids.bind_op (resolveWrite_reads_live hy ι)
    rw [after_resolveWrite]
    cases (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some q =>
      let w := Write.put (K := Key) (V := Val D) (.frameParam vid (pi.depth + 1) .wr idx) (.ptr q)
      have hwChild : ∀ k, w.affects k = true → inFrame vid (pi.depth + 1) k = true := by
        intro k hk; simp only [w, Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame]
      have hT' := write_child_preserves_nonforbidden (T := T) hwChild
      have hys' : ∀ z ∈ ys, WriteOpSafe vid pi (w.apply T) z :=
        fun z hz => writeOpSafe_of_agree_nonforbidden hT' (hys z (List.mem_cons_of_mem _ hz))
      exact ExecAvoids.wr_bind (no_bad_wr w) (ih (idx + 1) _ (w.apply T) hys')

private theorem bindChild_reads_live {vid : Vid} {pi : PC} {callee : SymName}
    {rdArgs wrArgs : List (Key × Val D)} {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdArgs, ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrArgs, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T := by
  unfold bindChild
  have hSig : ¬ IgnoredByAgree vid pi.depth (.libSig callee) := by
    rintro (h | h | h | h | h) <;> cases h
  apply ExecAvoids.rd_bind hSig
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
      apply ExecAvoids.wr_bind (no_bad_wr _)
      apply ExecAvoids.wr_bind (no_bad_wr _)
      apply ExecAvoids.wr_bind (no_bad_wr _)
      apply ExecAvoids.wr_bind (no_bad_wr _)
      let T₄ := (Write.put (.frameCallPc vid dChild) (.pc pi.entry)).apply
        ((Write.put (.frameRetPc vid dChild) (.pc pi.next)).apply
          ((Write.put (.frameLib vid dChild) (.sym callee)).apply
            ((Write.delWhere (inFrame vid dChild)).apply T)))
      have hT₄ : ∀ r, ForbiddenKey vid pi.depth r = false → T₄ r = T r := by
        intro r hr
        dsimp only [T₄]
        rw [write_child_preserves_nonforbidden
            (by intro k hk; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame, dChild]) r hr]
        rw [write_child_preserves_nonforbidden
            (by intro k hk; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame, dChild]) r hr]
        rw [write_child_preserves_nonforbidden
            (by intro k hk; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame, dChild]) r hr]
        exact write_child_preserves_nonforbidden
          (w := Write.delWhere (inFrame vid dChild)) (fun _ hk => hk) r hr
      have hrd₄ : ∀ x ∈ rdArgs, ReadOpSafe vid pi T₄ x :=
        fun x hx => readOpSafe_of_agree_nonforbidden hT₄ (hrd x hx)
      obtain ⟨_, hafRd⟩ := bindReadArgs_avoids (I := I) (E := E) 1 rdArgs ι T₄ hrd₄
      apply ExecAvoids.bind_op (bindReadArgs_reads_live 1 rdArgs ι T₄ hrd₄)
      cases (exec (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) ι T₄).1 with
      | error _ => exact ExecAvoids.ret _ _ _
      | ok _ =>
        have hwr₅ : ∀ y ∈ wrArgs, WriteOpSafe vid pi
            (after (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) ι T₄) y :=
          fun y hy => writeOpSafe_of_agree_nonforbidden
            (fun r hr => (hafRd r hr).trans (hT₄ r hr)) (hwr y hy)
        exact bindWriteArgs_reads_live 1 wrArgs _ _ hwr₅

private theorem readKeyList_reads_live {vid : Vid} {pi : PC} {ks : List Key}
    (hks : ∀ k ∈ ks, ForbiddenKey vid pi.depth k = false)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (readKeyList (D := D) (I := I) (E := E) ks) ι T := by
  induction ks generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons k ks ih =>
    unfold readKeyList
    apply ExecAvoids.rd_bind (not_ignored_of_not_forbidden (hks k List.mem_cons_self))
    apply ExecAvoids.bind_op
      (ih (fun k' hk' => hks k' (List.mem_cons_of_mem _ hk')) ι)
    exact ExecAvoids.ret _ _ _

private theorem recvMany_reads_live (vid : Vid) (d : Nat) (n : Nat)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IgnoredByAgree vid d) NoBadWr
      (recvMany (D := D) (I := I) (E := E) n) ι T := by
  induction n generalizing ι with
  | zero => exact ExecAvoids.ret _ _ _
  | succ n ih =>
    unfold recvMany
    apply ExecAvoids.recv_bind
    exact ExecAvoids.bind_op (ih (fun m => ι (m + 1))) (ExecAvoids.ret _ _ _)

private theorem emitAll_reads_live (vid : Vid) (d : Nat) (evs : List E)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IgnoredByAgree vid d) NoBadWr
      (emitAll (D := D) (I := I) evs) ι T := by
  induction evs generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons e evs ih =>
    unfold emitAll
    exact ExecAvoids.out_bind (ih ι)

private theorem putAll_reads_live (vid : Vid) (d : Nat) (kvs : List (Key × Val D))
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IgnoredByAgree vid d) NoBadWr
      (putAll (I := I) (E := E) kvs) ι T := by
  induction kvs generalizing ι T with
  | nil => exact ExecAvoids.ret _ _ _
  | cons kv kvs ih =>
    rcases kv with ⟨k, v⟩
    unfold putAll
    exact ExecAvoids.wr_bind (no_bad_wr _) (ih ι _)

private theorem ext_reads_live {ext : Extension D I E} (hExt : ExtensionSpec ext)
    (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D))
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
      (ext.run vid pi rdOps wrOps) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hmem
    exact not_ignored_of_not_forbidden (hExt.readsSafe vid pi rdOps wrOps ι T k v hmem)
  · intro _ _ _ _ h
    exact h

/-- Under `RuntimeSpec R` and `StepDiscipline vid T`, no action of `stepCore`
reads any key in `IgnoredByAgree vid (liveDepthOf vid T)`. -/
theorem stepCore_reads_not_ignored {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)} (hP : StepDiscipline vid T) (ι : Nat → I) :
    ExecAvoids (IgnoredByAgree vid (liveDepthOf vid T)) NoBadWr
      (stepCore R vid) ι T := by
  unfold stepCore
  apply ExecAvoids.rd_bind (pcKey_not_ignored vid (liveDepthOf vid T))
  cases hpc : T (.pcKey vid) with
  | none => exact ExecAvoids.ret _ _ _
  | some vPc =>
    cases vPc <;> try exact ExecAvoids.ret _ _ _
    rename_i pi
    rw [liveDepthOf_pc hpc]
    apply ExecAvoids.bind_op (decodeInstr_reads_live vid pi ι T)
    rw [after_decodeInstr]
    cases hdec : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 with
    | error _ => exact ExecAvoids.ret _ _ _
    | ok dec =>
      have hdecPure : decodeInstrPure vid pi T = .ok dec := by
        rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T]
        exact hdec
      obtain ⟨hrd, hwr⟩ := hP pi dec hpc hdecPure
      unfold dispatchInstr
      by_cases h1 : (dec.op == "=") = true
      · simp only [h1, ite_true]
        unfold copyRuleProg
        match dec.rdOps, dec.wrOps, hrd, hwr with
        | [rd1], [wr1], hrd, hwr =>
          have hr1 := hrd rd1 List.mem_cons_self
          have hw1 := hwr wr1 List.mem_cons_self
          apply ExecAvoids.bind_op (resolveRead_reads_live hr1 _)
          rw [after_resolveRead]
          cases hp : (exec (resolveRead (I := I) (E := E) vid pi rd1) _ T).1 with
          | none => exact ExecAvoids.ret _ _ _
          | some p =>
            have hp' := hp; rw [exec_resolveRead] at hp'
            apply ExecAvoids.bind_op (resolveWrite_reads_live hw1 _)
            rw [after_resolveWrite]
            cases (exec (resolveWrite (I := I) (E := E) vid pi wr1) _ T).1 with
            | none => exact ExecAvoids.ret _ _ _
            | some q =>
              apply ExecAvoids.rd_bind (not_ignored_of_not_forbidden (hr1.2 p hp'))
              cases T p with
              | none => exact ExecAvoids.ret _ _ _
              | some _ =>
                exact ExecAvoids.wr_bind (no_bad_wr _)
                  (ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _))
        | [], _, _, _ => exact ExecAvoids.ret _ _ _
        | _ :: _ :: _, _, _, _ => exact ExecAvoids.ret _ _ _
        | [_], [], _, _ => exact ExecAvoids.ret _ _ _
        | [_], _ :: _ :: _, _, _ => exact ExecAvoids.ret _ _ _
      · simp only [h1, Bool.false_eq_true, ite_false]
        by_cases h2 : (dec.op == "goto") = true
        · simp only [h2, ite_true]
          unfold gotoRuleProg
          match dec.rdOps, dec.wrOps, hrd with
          | [rd1], [], hrd =>
            apply ExecAvoids.bind_op (resolveRowTarget_reads_live (hrd rd1 List.mem_cons_self) _)
            cases (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) _ T).1 with
            | none => exact ExecAvoids.ret _ _ _
            | some _ => exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
          | [], _, _ => exact ExecAvoids.ret _ _ _
          | _ :: _ :: _, _, _ => exact ExecAvoids.ret _ _ _
          | [_], _ :: _, _ => exact ExecAvoids.ret _ _ _
        · simp only [h2, Bool.false_eq_true, ite_false]
          by_cases h3 : (dec.op == "br") = true
          · simp only [h3, ite_true]
            unfold brRuleProg
            match dec.rdOps, dec.wrOps, hrd with
            | [rd1, rd2, rd3], [], hrd =>
              have hr1 := hrd rd1 (by simp)
              have hr2 := hrd rd2 (by simp)
              have hr3 := hrd rd3 (by simp)
              obtain ⟨_, hafT⟩ := resolveTruth_avoids_and_after (I := I) (E := E) hr1 _
              apply ExecAvoids.bind_op (resolveTruth_reads_live hr1 _)
              rw [hafT]
              cases (exec (resolveTruth (I := I) (E := E) vid pi rd1) _ T).1 with
              | none => exact ExecAvoids.ret _ _ _
              | some b =>
                have hrb : ReadOpSafe vid pi T (if b then rd2 else rd3) := by cases b <;> assumption
                apply ExecAvoids.bind_op (resolveRowTarget_reads_live hrb _)
                cases (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) _ T).1 with
                | none => exact ExecAvoids.ret _ _ _
                | some _ => exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
            | [], _, _ => exact ExecAvoids.ret _ _ _
            | [_], _, _ => exact ExecAvoids.ret _ _ _
            | [_, _], _, _ => exact ExecAvoids.ret _ _ _
            | _ :: _ :: _ :: _ :: _, _, _ => exact ExecAvoids.ret _ _ _
            | [_, _, _], _ :: _, _ => exact ExecAvoids.ret _ _ _
          · simp only [h3, Bool.false_eq_true, ite_false]
            by_cases h4 : (dec.op == "call") = true
            · simp only [h4, ite_true]
              unfold callRuleProg
              match dec.rdOps, hrd with
              | [], _ => exact ExecAvoids.ret _ _ _
              | fnOp :: argRdOps, hrd =>
                have hfn := hrd fnOp List.mem_cons_self
                have hargs := fun x hx => hrd x (List.mem_cons_of_mem _ hx)
                apply ExecAvoids.bind_op (resolveRead_reads_live hfn _)
                rw [after_resolveRead]
                cases hp : (exec (resolveRead (I := I) (E := E) vid pi fnOp) _ T).1 with
                | none => exact ExecAvoids.ret _ _ _
                | some pFn =>
                  have hp' := hp; rw [exec_resolveRead] at hp'
                  apply ExecAvoids.rd_bind (not_ignored_of_not_forbidden (hfn.2 pFn hp'))
                  cases T pFn with
                  | none => exact ExecAvoids.ret _ _ _
                  | some vFn =>
                    cases vFn <;> try exact ExecAvoids.ret _ _ _
                    rename_i callee
                    apply ExecAvoids.bind_op (bindChild_reads_live hargs hwr _)
                    cases (exec (bindChild (I := I) (E := E) vid pi callee argRdOps dec.wrOps) _ T).1 with
                    | error _ => exact ExecAvoids.ret _ _ _
                    | ok _ => exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
            · simp only [h4, Bool.false_eq_true, ite_false]
              by_cases h5 : (dec.op == "return") = true
              · simp only [h5, ite_true]
                unfold returnRuleProg
                match dec.rdOps, dec.wrOps with
                | [], [] =>
                  by_cases hd : 1 < pi.depth
                  · simp only [hd, ite_true]
                    have hRetNotIg : ¬ IgnoredByAgree vid pi.depth (.frameRetPc vid pi.depth) := by
                      rintro (h | h | h | h | hdead) <;> try cases h
                      simp only [isDeadFrameKey, decide_eq_true_eq] at hdead
                      omega
                    apply ExecAvoids.rd_bind hRetNotIg
                    cases T (.frameRetPc vid pi.depth) with
                    | none => exact ExecAvoids.ret _ _ _
                    | some vRet =>
                      cases vRet <;> try exact ExecAvoids.ret _ _ _
                      rename_i r
                      cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) with
                      | true =>
                        simp only [hrv, ite_true]
                        exact ExecAvoids.wr_bind (no_bad_wr _)
                          (ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _))
                      | false =>
                        simp only [hrv, Bool.false_eq_true, ite_false]
                        exact ExecAvoids.ret _ _ _
                  · simp only [hd, ite_false]
                    exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
                | _ :: _, _ => exact ExecAvoids.ret _ _ _
                | [], _ :: _ => exact ExecAvoids.ret _ _ _
              · simp only [h5, Bool.false_eq_true, ite_false]
                by_cases h6 : R.caps dec.op = true
                · simp only [h6, ite_true]
                  unfold nativeRuleProg
                  obtain ⟨_, hafRd, hpsRd⟩ := resolveAllRead_avoids (I := I) (E := E) hrd _
                  apply ExecAvoids.bind_op (resolveAllRead_reads_live hrd _)
                  rw [hafRd]
                  cases hrdK : (exec (resolveAllRead (I := I) (E := E) vid pi dec.rdOps) _ T).1 with
                  | none => exact ExecAvoids.ret _ _ _
                  | some rdKeys =>
                    obtain ⟨_, hafWr, _⟩ := resolveAllWrite_avoids (I := I) (E := E) hwr _
                    apply ExecAvoids.bind_op (resolveAllWrite_reads_live hwr _)
                    rw [hafWr]
                    cases (exec (resolveAllWrite (I := I) (E := E) vid pi dec.wrOps) _ T).1 with
                    | none => exact ExecAvoids.ret _ _ _
                    | some wrKeys =>
                      obtain ⟨_, hafVals⟩ := readKeyList_avoids (I := I) (E := E) (hpsRd rdKeys hrdK) _ T
                      apply ExecAvoids.bind_op (readKeyList_reads_live (hpsRd rdKeys hrdK) _ T)
                      rw [hafVals]
                      apply ExecAvoids.bind_op (recvMany_reads_live vid pi.depth (R.native dec.op).numInputs _ T)
                      cases (R.native dec.op).eval wrKeys _ _ with
                      | error _ => exact ExecAvoids.ret _ _ _
                      | ok out =>
                        apply ExecAvoids.bind_op (emitAll_reads_live vid pi.depth out.events _ _)
                        apply ExecAvoids.bind_op (putAll_reads_live vid pi.depth out.writes _ _)
                        apply ExecAvoids.wr_bind (no_bad_wr _)
                        cases out.setStatus with
                        | none => exact ExecAvoids.ret _ _ _
                        | some _ => exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
                · simp only [h6, Bool.false_eq_true, ite_false]
                  have hQNotIg : ¬ IgnoredByAgree vid pi.depth (.queueEntry dec.op vid) := by
                    rintro (h | h | h | h | h) <;> cases h
                  have nonDeleg : ∀ ιh, ExecAvoids (IgnoredByAgree vid pi.depth) NoBadWr
                      (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T := by
                    intro ιh
                    have hHdr : ¬ IgnoredByAgree vid pi.depth (.libHeader dec.op) := by
                      rintro (h | h | h | h | h) <;> cases h
                    apply ExecAvoids.rd_bind hHdr
                    cases T (.libHeader dec.op) with
                    | none => exact ExecAvoids.ret _ _ _
                    | some vHdr =>
                      cases vHdr <;> try exact ExecAvoids.ret _ _ _
                      · unfold userCallRuleProg
                        apply ExecAvoids.bind_op (bindChild_reads_live hrd hwr _)
                        cases (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) _ T).1 with
                        | error _ => exact ExecAvoids.ret _ _ _
                        | ok _ => exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
                      · unfold handoffRuleProg
                        apply ExecAvoids.rd_bind hQNotIg
                        cases T (.queueEntry dec.op vid) with
                        | some _ => exact ExecAvoids.ret _ _ _
                        | none =>
                          apply ExecAvoids.wr_bind (no_bad_wr _)
                          apply ExecAvoids.bind_op
                            (ext_reads_live (hR.handoffH2 dec.op) vid pi dec.rdOps dec.wrOps _ _)
                          cases (exec ((R.handoffExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                          | error _ => exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
                          | ok out =>
                            apply ExecAvoids.bind_op (emitAll_reads_live vid pi.depth out.events _ _)
                            apply ExecAvoids.bind_op (putAll_reads_live vid pi.depth out.writes _ _)
                            apply ExecAvoids.wr_bind (no_bad_wr _)
                            exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
                  have hDel : ¬ IgnoredByAgree vid pi.depth (.delegEntry dec.op) := by
                    rintro (h | h | h | h | h) <;> cases h
                  apply ExecAvoids.rd_bind hDel
                  cases T (.delegEntry dec.op) with
                  | none => exact nonDeleg _
                  | some vDel =>
                    cases vDel <;> try exact nonDeleg _
                    unfold delegRuleProg
                    apply ExecAvoids.rd_bind hQNotIg
                    cases T (.queueEntry dec.op vid) with
                    | some _ => exact ExecAvoids.ret _ _ _
                    | none =>
                      apply ExecAvoids.wr_bind (no_bad_wr _)
                      apply ExecAvoids.wr_bind (no_bad_wr _)
                      apply ExecAvoids.bind_op
                        (ext_reads_live (hR.delegH2 dec.op) vid pi dec.rdOps dec.wrOps _ _)
                      cases (exec ((R.delegExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                      | error _ => exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)
                      | ok out =>
                        apply ExecAvoids.bind_op (emitAll_reads_live vid pi.depth out.events _ _)
                        apply ExecAvoids.bind_op (putAll_reads_live vid pi.depth out.writes _ _)
                        apply ExecAvoids.wr_bind (no_bad_wr _)
                        apply ExecAvoids.wr_bind (no_bad_wr _)
                        exact ExecAvoids.wr_bind (no_bad_wr _) (ExecAvoids.ret _ _ _)

/-- Read-congruence of `stepCore` across `StoreAgree`. -/
theorem stepCore_exec_congr_agree {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T U : Store Key (Val D)}
    (hAgree : StoreAgree vid T U) (hP : StepDiscipline vid T) (ι : Nat → I) :
    exec (stepCore R vid) ι U = exec (stepCore R vid) ι T := by
  have hav := stepCore_reads_not_ignored (E := E) hR hP ι
  exact exec_congr (stepCore R vid) ι T U (IgnoredByAgree vid (liveDepthOf vid T))
    (fun k hk => (hAgree.2 k hk).symm)
    (fun _ hexp => hav.not_exposed hexp)

/-! ### Post-State Characterization on Failed and Successful Steps -/

private theorem after_ext_eq {ext : Extension D I E} (hExt : ExtensionSpec ext)
    (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D))
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (ext.run vid pi rdOps wrOps) ι T = T := by
  unfold after
  rw [(hExt.internalPure vid pi rdOps wrOps ι T).1]
  rfl

private theorem after_resolveAllRead_eq (vid : Vid) (pi : PC)
    (xs : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveAllRead (I := I) (E := E) vid pi xs) ι T = T := by
  induction xs generalizing ι with
  | nil => rfl
  | cons x xs ih =>
    unfold resolveAllRead
    rw [after_bind_op, after_resolveRead]
    cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
    | none => rfl
    | some _ =>
      rw [after_bind_op, ih]
      cases (exec (resolveAllRead (I := I) (E := E) vid pi xs) _ T).1 <;> rfl

private theorem after_resolveAllWrite_eq (vid : Vid) (pi : PC)
    (ys : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveAllWrite (I := I) (E := E) vid pi ys) ι T = T := by
  induction ys generalizing ι with
  | nil => rfl
  | cons y ys ih =>
    unfold resolveAllWrite
    rw [after_bind_op, after_resolveWrite]
    cases (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
    | none => rfl
    | some _ =>
      rw [after_bind_op, ih]
      cases (exec (resolveAllWrite (I := I) (E := E) vid pi ys) _ T).1 <;> rfl

private theorem after_readKeyList_eq (ks : List Key) (ι : Nat → I) (T : Store Key (Val D)) :
    after (readKeyList (D := D) (I := I) (E := E) ks) ι T = T := by
  induction ks generalizing ι with
  | nil => rfl
  | cons k ks ih =>
    unfold readKeyList
    simp only [Prog.bind_eq, Prog.rd_bind, after_read]
    rw [after_bind, ih]; rfl

private theorem after_recvMany_eq (n : Nat) (ι : Nat → I) (T : Store Key (Val D)) :
    after (recvMany (D := D) (I := I) (E := E) n) ι T = T := by
  induction n generalizing ι with
  | zero => rfl
  | succ n ih =>
    unfold recvMany
    simp only [Prog.bind_eq, Prog.recv_bind, after_input]
    rw [after_bind, ih]; rfl

private theorem after_emitAll_eq (evs : List E) (ι : Nat → I) (T : Store Key (Val D)) :
    after (emitAll (D := D) (I := I) evs) ι T = T := by
  induction evs generalizing ι with
  | nil => rfl
  | cons e evs ih =>
    unfold emitAll
    simp only [Prog.bind_eq, Prog.out_bind, after_emit]
    exact ih ι

private theorem after_resolveRowTarget_eq (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveRowTarget (I := I) (E := E) vid pi x) ι T = T := by
  unfold resolveRowTarget
  rw [after_bind_op, after_resolveRead]
  cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => rfl
  | some p =>
    simp only [Prog.bind_eq, Prog.rd_bind, after_read]
    cases T p with
    | none => rfl
    | some v =>
      cases v <;> try rfl
      rename_i z
      cases z with
      | ofNat m => cases m <;> rfl
      | negSucc _ => rfl

private theorem after_resolveTruth_eq (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveTruth (I := I) (E := E) vid pi x) ι T = T := by
  unfold resolveTruth
  rw [after_bind_op, after_resolveRead]
  cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => rfl
  | some p =>
    simp only [Prog.bind_eq, Prog.rd_bind, after_read]
    cases T p with
    | none => rfl
    | some v => cases v <;> rfl

private theorem bindReadArgs_writes_child (vid : Vid) (pi : PC) (idx : Nat)
    (xs : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (fun k => inFrame vid (pi.depth + 1) k = false)
      (bindReadArgs (I := I) (E := E) vid pi idx xs) ι T := by
  induction xs generalizing idx ι T with
  | nil => exact ExecAvoids.ret _ _ _
  | cons x xs ih =>
    unfold bindReadArgs
    have havRes : ExecAvoids (fun _ => False) (fun k => inFrame vid (pi.depth + 1) k = false)
        (resolveRead (I := I) (E := E) vid pi x) ι T := by
      refine ⟨fun _ _ _ h => h, ?_⟩
      intro w hm; rw [exec_resolveRead] at hm; simp at hm
    apply ExecAvoids.bind_op havRes
    rw [after_resolveRead]
    cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some p =>
      apply ExecAvoids.wr_bind
      · intro k hk hfalse
        simp only [Write.affects, decide_eq_true_eq] at hk
        subst hk
        simp [inFrame] at hfalse
      · exact ih (idx + 1) _ _

private theorem bindWriteArgs_writes_child (vid : Vid) (pi : PC) (idx : Nat)
    (ys : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (fun k => inFrame vid (pi.depth + 1) k = false)
      (bindWriteArgs (I := I) (E := E) vid pi idx ys) ι T := by
  induction ys generalizing idx ι T with
  | nil => exact ExecAvoids.ret _ _ _
  | cons y ys ih =>
    unfold bindWriteArgs
    have havRes : ExecAvoids (fun _ => False) (fun k => inFrame vid (pi.depth + 1) k = false)
        (resolveWrite (I := I) (E := E) vid pi y) ι T := by
      refine ⟨fun _ _ _ h => h, ?_⟩
      intro w hm; rw [exec_resolveWrite] at hm; simp at hm
    apply ExecAvoids.bind_op havRes
    rw [after_resolveWrite]
    cases (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some q =>
      apply ExecAvoids.wr_bind
      · intro k hk hfalse
        simp only [Write.affects, decide_eq_true_eq] at hk
        subst hk
        simp [inFrame] at hfalse
      · exact ih (idx + 1) _ _

private theorem bindChild_writes_child (vid : Vid) (pi : PC) (callee : SymName)
    (rdArgs wrArgs : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (fun k => inFrame vid (pi.depth + 1) k = false)
      (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T := by
  unfold bindChild
  apply ExecAvoids.rd_bind (fun h => h)
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
      apply ExecAvoids.wr_bind
      · intro k hk hfalse; simp [Write.affects] at hk; rw [hk] at hfalse; cases hfalse
      apply ExecAvoids.wr_bind
      · intro k hk hfalse; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame] at hfalse
      apply ExecAvoids.wr_bind
      · intro k hk hfalse; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame] at hfalse
      apply ExecAvoids.wr_bind
      · intro k hk hfalse; simp only [Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame] at hfalse
      apply ExecAvoids.bind_op (bindReadArgs_writes_child vid pi 1 rdArgs _ _)
      cases (exec (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) _ _).1 with
      | error _ => exact ExecAvoids.ret _ _ _
      | ok _ => exact bindWriteArgs_writes_child vid pi 1 wrArgs _ _

private theorem storeAgree_of_only_child {vid : Vid} {pi : PC}
    {P : Prog Key (Val D) I E (Except ErrMsg Unit)} {ι : Nat → I}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hav : ExecAvoids (fun _ => False) (fun k => inFrame vid (pi.depth + 1) k = false) P ι T) :
    StoreAgree vid (after P ι T) T := by
  have hpcEq : after P ι T (.pcKey vid) = T (.pcKey vid) :=
    hav.after_eq (inFrame_pcKey vid vid (pi.depth + 1))
  refine ⟨hpcEq, ?_⟩
  intro k hk
  rw [liveDepthOf_eq_of_pc_eq hpcEq, liveDepthOf_pc hpc] at hk
  apply hav.after_eq
  cases hin : inFrame vid (pi.depth + 1) k with
  | false => rfl
  | true =>
    have hdead := inFrame_iff_dead_of_gt (by omega : pi.depth < pi.depth + 1) hin
    exact absurd (Or.inr (Or.inr (Or.inr (Or.inr hdead)))) hk

/-- Any failed `stepCore` attempt leaves a store `StoreAgree`-equivalent to `T`. -/
theorem failed_stepCore_preserves_agree {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)} {ι : Nat → I} {err : ErrMsg}
    (hFail : (exec (stepCore R vid) ι T).1 = .error err) :
    StoreAgree vid (after (stepCore R vid) ι T) T := by
  unfold stepCore at hFail ⊢
  simp only [Prog.bind_eq, Prog.rd_bind] at hFail ⊢
  rw [exec_read] at hFail
  rw [after_read]
  cases hpc : T (.pcKey vid) with
  | none => exact StoreAgree.refl vid T
  | some vPc =>
    rw [hpc] at hFail
    cases vPc <;> try exact StoreAgree.refl vid T
    rename_i pi
    simp only [] at hFail ⊢
    rw [← Prog.bind_eq, exec_bind_fst, after_decodeInstr] at hFail
    rw [← Prog.bind_eq, after_bind_op, after_decodeInstr]
    cases hdec : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 with
    | error _ => exact StoreAgree.refl vid T
    | ok dec =>
      rw [hdec] at hFail
      simp only [] at hFail ⊢
      let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
      change (exec (dispatchInstr R vid pi dec) ιd T).1 = .error err at hFail
      change StoreAgree vid (after (dispatchInstr R vid pi dec) ιd T) T
      unfold dispatchInstr at hFail ⊢
      by_cases h1 : (dec.op == "=") = true
      · simp only [h1, ite_true] at hFail ⊢
        unfold copyRuleProg at hFail ⊢
        match dec.rdOps, dec.wrOps, hFail with
        | [rd1], [wr1], hFail =>
          dsimp only [] at hFail ⊢
          rw [exec_bind_fst, after_resolveRead] at hFail
          rw [after_bind_op, after_resolveRead]
          cases hp : (exec (resolveRead (I := I) (E := E) vid pi rd1) ιd T).1 with
          | none => exact StoreAgree.refl vid T
          | some p =>
            rw [hp] at hFail
            dsimp only [] at hFail ⊢
            rw [exec_bind_fst, after_resolveWrite] at hFail
            rw [after_bind_op, after_resolveWrite]
            cases hq : (exec (resolveWrite (I := I) (E := E) vid pi wr1) _ T).1 with
            | none => exact StoreAgree.refl vid T
            | some q =>
              rw [hq] at hFail
              dsimp only [] at hFail ⊢
              simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hFail ⊢
              cases hpVal : T p with
              | none => exact StoreAgree.refl vid T
              | some val =>
                rw [hpVal] at hFail
                simp [Prog.wr_bind, exec_write, exec] at hFail
        | [], _, _ => exact StoreAgree.refl vid T
        | _ :: _ :: _, _, _ => exact StoreAgree.refl vid T
        | [_], [], _ => exact StoreAgree.refl vid T
        | [_], _ :: _ :: _, _ => exact StoreAgree.refl vid T
      · simp only [h1, Bool.false_eq_true, ite_false] at hFail ⊢
        by_cases h2 : (dec.op == "goto") = true
        · simp only [h2, ite_true] at hFail ⊢
          unfold gotoRuleProg at hFail ⊢
          match dec.rdOps, dec.wrOps, hFail with
          | [rd1], [], hFail =>
            dsimp only [] at hFail ⊢
            rw [exec_bind_fst, after_resolveRowTarget_eq] at hFail
            rw [after_bind_op, after_resolveRowTarget_eq]
            cases hrt : (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) ιd T).1 with
            | none => exact StoreAgree.refl vid T
            | some n =>
              rw [hrt] at hFail
              simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hFail
          | [], _, _ => exact StoreAgree.refl vid T
          | _ :: _ :: _, _, _ => exact StoreAgree.refl vid T
          | [_], _ :: _, _ => exact StoreAgree.refl vid T
        · simp only [h2, Bool.false_eq_true, ite_false] at hFail ⊢
          by_cases h3 : (dec.op == "br") = true
          · simp only [h3, ite_true] at hFail ⊢
            unfold brRuleProg at hFail ⊢
            match dec.rdOps, dec.wrOps, hFail with
            | [rd1, rd2, rd3], [], hFail =>
              dsimp only [] at hFail ⊢
              rw [exec_bind_fst, after_resolveTruth_eq] at hFail
              rw [after_bind_op, after_resolveTruth_eq]
              cases htr : (exec (resolveTruth (I := I) (E := E) vid pi rd1) ιd T).1 with
              | none => exact StoreAgree.refl vid T
              | some b =>
                rw [htr] at hFail
                dsimp only [] at hFail ⊢
                rw [exec_bind_fst, after_resolveRowTarget_eq] at hFail
                rw [after_bind_op, after_resolveRowTarget_eq]
                cases hrt : (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) _ T).1 with
                | none => exact StoreAgree.refl vid T
                | some n =>
                  rw [hrt] at hFail
                  simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hFail
            | [], _, _ => exact StoreAgree.refl vid T
            | [_], _, _ => exact StoreAgree.refl vid T
            | [_, _], _, _ => exact StoreAgree.refl vid T
            | _ :: _ :: _ :: _ :: _, _, _ => exact StoreAgree.refl vid T
            | [_, _, _], _ :: _, _ => exact StoreAgree.refl vid T
          · simp only [h3, Bool.false_eq_true, ite_false] at hFail ⊢
            by_cases h4 : (dec.op == "call") = true
            · simp only [h4, ite_true] at hFail ⊢
              unfold callRuleProg at hFail ⊢
              match dec.rdOps, hFail with
              | [], _ => exact StoreAgree.refl vid T
              | fnOp :: argRdOps, hFail =>
                dsimp only [] at hFail ⊢
                rw [exec_bind_fst, after_resolveRead] at hFail
                rw [after_bind_op, after_resolveRead]
                cases hp : (exec (resolveRead (I := I) (E := E) vid pi fnOp) ιd T).1 with
                | none => exact StoreAgree.refl vid T
                | some pFn =>
                  rw [hp] at hFail
                  dsimp only [] at hFail ⊢
                  simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hFail ⊢
                  cases hvFn : T pFn with
                  | none => exact StoreAgree.refl vid T
                  | some vFn =>
                    rw [hvFn] at hFail
                    cases vFn <;> try exact StoreAgree.refl vid T
                    rename_i callee
                    dsimp only [] at hFail ⊢
                    rw [← Prog.bind_eq, exec_bind_fst] at hFail
                    rw [← Prog.bind_eq, after_bind_op]
                    cases hb : (exec (bindChild (I := I) (E := E) vid pi callee argRdOps dec.wrOps) _ T).1 with
                    | error _ =>
                      exact storeAgree_of_only_child hpc
                        (bindChild_writes_child vid pi callee argRdOps dec.wrOps _ T)
                    | ok u =>
                      rw [hb] at hFail
                      cases u
                      simp [Prog.wr_bind, exec_write, exec] at hFail
            · simp only [h4, Bool.false_eq_true, ite_false] at hFail ⊢
              by_cases h5 : (dec.op == "return") = true
              · simp only [h5, ite_true] at hFail ⊢
                unfold returnRuleProg at hFail ⊢
                match dec.rdOps, dec.wrOps, hFail with
                | [], [], hFail =>
                  dsimp only [] at hFail ⊢
                  by_cases hd : 1 < pi.depth
                  · simp only [hd, ite_true, Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hFail ⊢
                    cases hvRet : T (.frameRetPc vid pi.depth) with
                    | none => exact StoreAgree.refl vid T
                    | some vRet =>
                      rw [hvRet] at hFail
                      cases vRet <;> try exact StoreAgree.refl vid T
                      rename_i r
                      dsimp only [] at hFail ⊢
                      cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) with
                      | false =>
                        simp only [Bool.false_eq_true, ite_false]
                        exact StoreAgree.refl vid T
                      | true =>
                        rw [hrv] at hFail
                        simp [Prog.wr_bind, exec_write, exec] at hFail
                  · simp [hd, Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hFail
                | _ :: _, _, _ => exact StoreAgree.refl vid T
                | [], _ :: _, _ => exact StoreAgree.refl vid T
              · simp only [h5, Bool.false_eq_true, ite_false] at hFail ⊢
                by_cases h6 : R.caps dec.op = true
                · simp only [h6, ite_true] at hFail ⊢
                  unfold nativeRuleProg at hFail ⊢
                  rw [exec_bind_fst, after_resolveAllRead_eq] at hFail
                  rw [after_bind_op, after_resolveAllRead_eq]
                  cases hrdK : (exec (resolveAllRead (I := I) (E := E) vid pi dec.rdOps) ιd T).1 with
                  | none => exact StoreAgree.refl vid T
                  | some rdKeys =>
                    rw [hrdK] at hFail
                    dsimp only [] at hFail ⊢
                    rw [exec_bind_fst, after_resolveAllWrite_eq] at hFail
                    rw [after_bind_op, after_resolveAllWrite_eq]
                    cases hwrK : (exec (resolveAllWrite (I := I) (E := E) vid pi dec.wrOps) _ T).1 with
                    | none => exact StoreAgree.refl vid T
                    | some wrKeys =>
                      rw [hwrK] at hFail
                      dsimp only [] at hFail ⊢
                      rw [exec_bind_fst, after_readKeyList_eq] at hFail
                      rw [after_bind_op, after_readKeyList_eq]
                      rw [exec_bind_fst, after_recvMany_eq] at hFail
                      rw [after_bind_op, after_recvMany_eq]
                      cases hEv : (R.native dec.op).eval wrKeys _ _ with
                      | error _ => exact StoreAgree.refl vid T
                      | ok out =>
                        rw [hEv] at hFail
                        dsimp only [] at hFail
                        rw [exec_bind_fst] at hFail
                        rw [exec_bind_fst] at hFail
                        simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hFail
                        cases hSet : out.setStatus <;> simp [hSet] at hFail
                · simp only [h6, Bool.false_eq_true, ite_false] at hFail ⊢
                  have nonDelegFail : ∀ ιh,
                      (exec (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T).1 = .error err →
                      StoreAgree vid (after (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T) T := by
                    intro ιh hF
                    rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hF
                    rw [Prog.bind_eq, Prog.rd_bind, after_read]
                    cases hvHdr : T (.libHeader dec.op) with
                    | none => exact StoreAgree.refl vid T
                    | some vHdr =>
                      rw [hvHdr] at hF
                      cases vHdr <;> try exact StoreAgree.refl vid T
                      · dsimp only [] at hF ⊢
                        unfold userCallRuleProg at hF ⊢
                        rw [exec_bind_fst] at hF
                        rw [after_bind_op]
                        cases hb : (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ιh T).1 with
                        | error _ =>
                          exact storeAgree_of_only_child hpc
                            (bindChild_writes_child vid pi dec.op dec.rdOps dec.wrOps ιh T)
                        | ok u =>
                          rw [hb] at hF
                          cases u
                          simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hF
                      · dsimp only [] at hF ⊢
                        unfold handoffRuleProg at hF ⊢
                        simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hF ⊢
                        cases hq : T (.queueEntry dec.op vid) with
                        | some _ => exact StoreAgree.refl vid T
                        | none =>
                          rw [hq] at hF
                          dsimp only [] at hF ⊢
                          simp only [Prog.wr_bind, exec_write, after_write] at hF ⊢
                          rw [← Prog.bind_eq, exec_bind_fst] at hF
                          rw [← Prog.bind_eq, after_bind_op, after_ext_eq (hR.handoffH2 dec.op)]
                          cases hex : (exec ((R.handoffExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                          | error _ =>
                            dsimp only []
                            rw [after_write]
                            dsimp only [after, exec, Act.writes, applyAll]
                            have hEq : (Write.del (.queueEntry dec.op vid)).apply
                                ((Write.put (.queueEntry dec.op vid) (.pc pi)).apply T) = T := by
                              funext k
                              by_cases hkq : k = .queueEntry dec.op vid
                              · subst hkq
                                simp [Write.apply, Write.affects, Write.newVal, hq]
                              · simp [Write.apply, Write.affects, hkq]
                            rw [hEq]
                            exact StoreAgree.refl vid T
                          | ok out =>
                            rw [hex] at hF
                            dsimp only [] at hF
                            rw [← Prog.bind_eq, exec_bind_fst] at hF
                            rw [← Prog.bind_eq, exec_bind_fst] at hF
                            simp [exec] at hF
                  rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFail
                  rw [Prog.bind_eq, Prog.rd_bind, after_read]
                  cases hvDel : T (.delegEntry dec.op) with
                  | none =>
                    rw [hvDel] at hFail
                    exact nonDelegFail _ hFail
                  | some vDel =>
                    rw [hvDel] at hFail
                    cases vDel <;> try exact nonDelegFail _ hFail
                    dsimp only [] at hFail ⊢
                    unfold delegRuleProg at hFail ⊢
                    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hFail ⊢
                    cases hq : T (.queueEntry dec.op vid) with
                    | some _ => exact StoreAgree.refl vid T
                    | none =>
                      rw [hq] at hFail
                      dsimp only [] at hFail ⊢
                      simp only [Prog.wr_bind, exec_write, after_write] at hFail ⊢
                      rw [← Prog.bind_eq, exec_bind_fst] at hFail
                      rw [← Prog.bind_eq, after_bind_op, after_ext_eq (hR.delegH2 dec.op)]
                      cases hex : (exec ((R.delegExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                      | error _ =>
                        dsimp only []
                        rw [after_write]
                        dsimp only [after, exec, Act.writes, applyAll]
                        have hEq : (Write.del (.queueEntry dec.op vid)).apply
                            ((Write.put (.queueEntry dec.op vid) (.pc pi)).apply
                              ((Write.put (.statusKey vid) (.status .wait)).apply T)) =
                            (Write.put (.statusKey vid) (.status .wait)).apply T := by
                          funext k
                          by_cases hkq : k = .queueEntry dec.op vid
                          · subst hkq
                            simp [Write.apply, Write.affects, Write.newVal, hq]
                          · simp [Write.apply, Write.affects, hkq]
                        rw [hEq]
                        exact storeAgree_putStatus vid .wait T
                      | ok out =>
                        rw [hex] at hFail
                        dsimp only [] at hFail
                        rw [← Prog.bind_eq, exec_bind_fst] at hFail
                        rw [← Prog.bind_eq, exec_bind_fst] at hFail
                        simp [exec] at hFail

/-- A failed `gatedStep` from a store with `rho = none` preserves `StoreAgree`. -/
theorem failed_gatedStep_preserves_agree {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)} {ι : Nat → I} {err : ErrMsg}
    (hrho : T (.stopKey vid) = none)
    (hFail : (exec (stepCore R vid) ι T).1 = .error err) :
    StoreAgree vid (after (gatedStep R vid) ι T) T := by
  have hCore := failed_stepCore_preserves_agree hR hFail
  have hAfterEq : after (gatedStep R vid) ι T =
      (Write.put (.statusKey vid) (.status .error)).apply
        ((Write.put (.msgKey vid) (.errMsg err)).apply (after (stepCore R vid) ι T)) := by
    unfold gatedStep
    simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrho]
    try dsimp only []
    rw [after_bind, hFail]
    dsimp only [finalizeStep]
    simp only [Prog.bind_eq, Prog.wr_bind, after_write]
    rfl
  rw [hAfterEq]
  exact (storeAgree_putStatus vid .error _).trans
    ((storeAgree_putMsg vid err _).trans hCore)

private theorem bindChild_has_delWhere {vid : Vid} {pi : PC} {callee : SymName}
    {rdArgs wrArgs : List (Key × Val D)} {ι : Nat → I} {T : Store Key (Val D)}
    (hok : (exec (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T).1 = .ok ()) :
    Write.delWhere (inFrame vid (pi.depth + 1)) ∈
      Act.writes (exec (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T).2 := by
  unfold bindChild at hok ⊢
  rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hok ⊢
  cases hvSig : T (.libSig callee) with
  | none => rw [hvSig] at hok; simp at hok
  | some vSig =>
    rw [hvSig] at hok
    cases vSig <;> try (simp at hok; done)
    rename_i sigNr sigNw
    dsimp only [] at hok ⊢
    cases har : (sigNr != rdArgs.length || sigNw != wrArgs.length) with
    | true => rw [har] at hok; simp at hok
    | false =>
      simp only [Bool.false_eq_true, ite_false, Prog.bind_eq, Prog.wr_bind, exec_write,
        Act.writes_read, Act.writes_write]
      exact List.mem_cons_self

private theorem not_status_of_not_forbidden {vid : Vid} {d : Nat} {k : Key}
    (h : ForbiddenKey vid d k = false) : k ≠ .statusKey vid := by
  rintro rfl
  simp [ForbiddenKey, isThreadControlKey] at h

private theorem put_avoids_status {vid : Vid} {k : Key} (hk : k ≠ .statusKey vid) (v : Val D) :
    ∀ k', (Write.put k v).affects k' = true → k' ≠ .statusKey vid := by
  intro k' haff
  simp only [Write.affects, decide_eq_true_eq] at haff
  subst haff
  exact hk

private theorem del_avoids_status {vid : Vid} {k : Key} (hk : k ≠ .statusKey vid) :
    ∀ k', (Write.del (V := Val D) k).affects k' = true → k' ≠ .statusKey vid := by
  intro k' haff
  simp only [Write.affects, decide_eq_true_eq] at haff
  subst haff
  exact hk

private theorem delFrame_avoids_status (vid : Vid) (d : Nat) :
    ∀ k', (Write.delWhere (V := Val D) (inFrame vid d)).affects k' = true →
      k' ≠ .statusKey vid := by
  rintro k' haff rfl
  simp [Write.affects, inFrame] at haff

private theorem readOnly_avoids_status {vid : Vid} {α : Type} {P : Prog Key (Val D) I E α}
    {ι : Nat → I} {T : Store Key (Val D)}
    (hw : Act.writes (exec P ι T).2 = []) :
    ExecAvoids (fun _ => False) (· = .statusKey vid) P ι T := by
  refine ⟨fun _ _ _ h => h, ?_⟩
  intro w hm
  have hmem := Act.mem_writes_of_mem hm
  rw [hw] at hmem
  simp at hmem

private theorem writes_resolveRead_nil (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    Act.writes (exec (resolveRead (I := I) (E := E) vid pi x) ι T).2 = [] := by
  rw [exec_resolveRead]
  induction resolveReadReads vid pi x T with
  | nil => rfl
  | cons _ _ ih => simp [ih]

private theorem writes_resolveWrite_nil (vid : Vid) (pi : PC) (y : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    Act.writes (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).2 = [] := by
  rw [exec_resolveWrite]
  induction resolveWriteReads vid pi y T with
  | nil => rfl
  | cons _ _ ih => simp [ih]

private theorem resolveAllRead_avoids_status (vid : Vid) (pi : PC)
    (xs : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (resolveAllRead (I := I) (E := E) vid pi xs) ι T := by
  induction xs generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons x xs ih =>
    unfold resolveAllRead
    apply ExecAvoids.bind_op (readOnly_avoids_status (writes_resolveRead_nil vid pi x ι T))
    rw [after_resolveRead]
    cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some _ =>
      apply ExecAvoids.bind_op (ih _)
      cases (exec (resolveAllRead (I := I) (E := E) vid pi xs) _ T).1 <;>
        exact ExecAvoids.ret _ _ _

private theorem resolveAllWrite_avoids_status (vid : Vid) (pi : PC)
    (ys : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (resolveAllWrite (I := I) (E := E) vid pi ys) ι T := by
  induction ys generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons y ys ih =>
    unfold resolveAllWrite
    apply ExecAvoids.bind_op (readOnly_avoids_status (writes_resolveWrite_nil vid pi y ι T))
    rw [after_resolveWrite]
    cases (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some _ =>
      apply ExecAvoids.bind_op (ih _)
      cases (exec (resolveAllWrite (I := I) (E := E) vid pi ys) _ T).1 <;>
        exact ExecAvoids.ret _ _ _

private theorem readKeyList_avoids_status (vid : Vid) (ks : List Key)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (readKeyList (D := D) (I := I) (E := E) ks) ι T := by
  induction ks generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons k ks ih =>
    unfold readKeyList
    exact ExecAvoids.rd_bind (fun h => h)
      (ExecAvoids.bind_op (ih ι) (ExecAvoids.ret _ _ _))

private theorem recvMany_avoids_status (vid : Vid) (n : Nat)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (recvMany (D := D) (I := I) (E := E) n) ι T := by
  induction n generalizing ι with
  | zero => exact ExecAvoids.ret _ _ _
  | succ n ih =>
    unfold recvMany
    exact ExecAvoids.recv_bind
      (ExecAvoids.bind_op (ih (fun m => ι (m + 1))) (ExecAvoids.ret _ _ _))

private theorem emitAll_avoids_status (vid : Vid) (evs : List E)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (emitAll (D := D) (I := I) evs) ι T := by
  induction evs generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons _ evs ih =>
    unfold emitAll
    exact ExecAvoids.out_bind (ih ι)

private theorem putAll_avoids_status {vid : Vid} {pi : PC} {kvs : List (Key × Val D)}
    (hkvs : ∀ k v, (k, v) ∈ kvs → ForbiddenKey vid pi.depth k = false)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (putAll (I := I) (E := E) kvs) ι T := by
  induction kvs generalizing ι T with
  | nil => exact ExecAvoids.ret _ _ _
  | cons kv kvs ih =>
    rcases kv with ⟨k, v⟩
    unfold putAll
    exact ExecAvoids.wr_bind
      (put_avoids_status (not_status_of_not_forbidden (hkvs k v List.mem_cons_self)) v)
      (ih (fun k' v' hm => hkvs k' v' (List.mem_cons_of_mem _ hm)) ι _)

private theorem bindChild_avoids_status (vid : Vid) (pi : PC) (callee : SymName)
    (rdArgs wrArgs : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T := by
  have hav := bindChild_writes_child (I := I) (E := E) vid pi callee rdArgs wrArgs ι T
  refine ⟨fun _ _ _ h => h, ?_⟩
  rintro w hm k hk rfl
  exact hav.2 w hm (.statusKey vid) hk (inFrame_statusKey vid vid (pi.depth + 1))

private theorem resolveRowTarget_avoids_status (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (resolveRowTarget (I := I) (E := E) vid pi x) ι T := by
  unfold resolveRowTarget
  apply ExecAvoids.bind_op (readOnly_avoids_status (writes_resolveRead_nil vid pi x ι T))
  rw [after_resolveRead]
  cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => exact ExecAvoids.ret _ _ _
  | some p =>
    apply ExecAvoids.rd_bind (fun h => h)
    cases T p with
    | none => exact ExecAvoids.ret _ _ _
    | some v =>
      cases v <;> try exact ExecAvoids.ret _ _ _
      rename_i z
      cases z with
      | ofNat m => cases m <;> exact ExecAvoids.ret _ _ _
      | negSucc _ => exact ExecAvoids.ret _ _ _

private theorem resolveTruth_avoids_status (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (fun _ => False) (· = .statusKey vid)
      (resolveTruth (I := I) (E := E) vid pi x) ι T := by
  unfold resolveTruth
  apply ExecAvoids.bind_op (readOnly_avoids_status (writes_resolveRead_nil vid pi x ι T))
  rw [after_resolveRead]
  cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => exact ExecAvoids.ret _ _ _
  | some p =>
    apply ExecAvoids.rd_bind (fun h => h)
    cases T p with
    | none => exact ExecAvoids.ret _ _ _
    | some v => cases v <;> exact ExecAvoids.ret _ _ _

/-- Post-state properties of a successful `stepCore` under `RuntimeSpec` and `(P)`:
1. `T' (.pcKey vid) = some (.pc pi')`,
2. `pi'.depth ≤ pi.depth` or (`pi'.depth = pi.depth + 1` with child-frame `delWhere`),
3. Either `.statusKey vid` is untouched or set to a non-`error` status. -/
theorem stepCore_ok_post {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)} (hP : StepDiscipline vid T) {ι : Nat → I}
    {pi' : PC} {evs : List E} {asCore : List (Act Key (Val D) I E)}
    (hOk : exec (stepCore R vid) ι T = (.ok (pi', evs), asCore)) :
    ∃ pi,
      T (.pcKey vid) = some (.pc pi) ∧
      pi.valid = true ∧
      applyAll T (Act.writes asCore) (.pcKey vid) = some (.pc pi') ∧
      (pi'.depth ≤ pi.depth ∨
        (pi'.depth = pi.depth + 1 ∧
         Write.delWhere (inFrame vid (pi.depth + 1)) ∈ Act.writes asCore)) ∧
      (touches (Act.writes asCore) (.statusKey vid) = false ∨
        ∃ s', applyAll T (Act.writes asCore) (.statusKey vid) = some (.status s') ∧ s' ≠ .error) := by
  have hFst : (exec (stepCore R vid) ι T).1 = .ok (pi', evs) := by rw [hOk]
  have hAfterEq : applyAll T (Act.writes asCore) = after (stepCore R vid) ι T := by
    unfold after; rw [hOk]
  have hWritesEq : Act.writes asCore = Act.writes (exec (stepCore R vid) ι T).2 := by
    rw [hOk]
  rw [hAfterEq, hWritesEq]
  clear hOk asCore hAfterEq hWritesEq
  unfold stepCore at hFst ⊢
  simp only [Prog.bind_eq, Prog.rd_bind] at hFst ⊢
  rw [exec_read] at hFst
  cases hpc : T (.pcKey vid) with
  | none => rw [hpc] at hFst; simp at hFst
  | some vPc =>
    rw [hpc] at hFst
    cases vPc <;> try (simp at hFst)
    rename_i pi
    refine ⟨pi, rfl, ?_⟩
    simp only [exec_read, hpc, after_read, Act.writes_read] at hFst ⊢
    rw [← Prog.bind_eq, exec_bind_fst, after_decodeInstr] at hFst
    cases hdec : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 with
    | error _ => rw [hdec] at hFst; simp at hFst
    | ok dec =>
      have hPure : decodeInstrPure vid pi T = .ok dec := by
        rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T]; exact hdec
      have hpv : pi.valid = true := by
        unfold decodeInstrPure at hPure
        cases hv : pi.valid with
        | true => rfl
        | false => simp [hv] at hPure
      obtain ⟨hrd, hwr⟩ := hP pi dec hpc hPure
      refine ⟨hpv, ?_⟩
      rw [hdec] at hFst
      simp only [] at hFst
      have hDecWrNil : Act.writes (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).2 = [] := by
        unfold decodeInstr
        simp only [hpv, Bool.not_true, Bool.false_eq_true, ite_false, Prog.bind_eq, Prog.rd_bind, exec_read]
        cases T (.frameLib vid pi.depth) with
        | none => rfl
        | some vL =>
          cases vL <;> try rfl
          rename_i fn
          dsimp only []
          simp only [exec_read, Act.writes_read]
          cases T (.libInstr fn pi.row) with
          | none => rfl
          | some vI =>
            cases vI <;> try rfl
            rename_i op nr nw
            dsimp only []
            have hOpWr : ∀ dir idx cnt ι₀,
                Act.writes (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row dir idx cnt) ι₀ T).2 = [] := by
              intro dir idx cnt
              induction cnt generalizing idx with
              | zero => intro _; rfl
              | succ cnt ih =>
                intro ι₀
                unfold readOperandsFrom
                simp only [Prog.bind_eq, Prog.rd_bind, exec_read, Act.writes_read]
                cases T (.libOperand fn pi.row dir idx) with
                | none => rfl
                | some _ =>
                  dsimp only []
                  rw [exec_bind]
                  dsimp only []
                  rw [Act.writes_append, ih (idx + 1)]
                  cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row dir (idx + 1) cnt) _ T).1 <;> rfl
            rw [exec_bind]
            dsimp only []
            rw [Act.writes_append, hOpWr .rd 1 nr, after_readOperandsFrom]
            cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .rd 1 nr) _ T).1 with
            | none => rfl
            | some _ =>
              dsimp only [List.nil_append]
              rw [exec_bind]
              dsimp only []
              rw [Act.writes_append, hOpWr .wr 1 nw]
              cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .wr 1 nw) _ T).1 <;> rfl
      let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
      rw [exec_bind, after_bind]
      dsimp only []
      rw [hdec, after_decodeInstr]
      dsimp only []
      rw [Act.writes_append, hDecWrNil, List.nil_append]
      have hPcNotStatus : Key.pcKey vid ≠ .statusKey vid := by intro h; cases h
      unfold dispatchInstr at hFst ⊢
      by_cases h1 : (dec.op == "=") = true
      · simp only [h1, ite_true] at hFst ⊢
        unfold copyRuleProg at hFst ⊢
        match dec.rdOps, dec.wrOps, hwr, hFst with
        | [rd1], [wr1], hwr, hFst =>
          dsimp only [] at hFst ⊢
          have hw1 := hwr wr1 List.mem_cons_self
          rw [exec_bind_fst, after_resolveRead] at hFst
          cases hp : (exec (resolveRead (I := I) (E := E) vid pi rd1) ιd T).1 with
          | none => rw [hp] at hFst; simp at hFst
          | some p =>
            rw [hp] at hFst; dsimp only [] at hFst
            rw [exec_bind_fst, after_resolveWrite] at hFst
            cases hq : (exec (resolveWrite (I := I) (E := E) vid pi wr1) _ T).1 with
            | none => rw [hq] at hFst; simp at hFst
            | some q =>
              have hq' := hq; rw [exec_resolveWrite] at hq'
              have hqNotSt : q ≠ .statusKey vid := not_status_of_not_forbidden (hw1.2 q hq').1
              rw [hq] at hFst; dsimp only [] at hFst
              simp only [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
              cases hv : T p with
              | none => rw [hv] at hFst; simp at hFst
              | some val =>
                rw [hv] at hFst; dsimp only [] at hFst
                simp only [Prog.wr_bind, exec_write] at hFst
                simp at hFst
                rcases hFst with ⟨rfl, rfl⟩
                have havSt : ExecAvoids (fun _ => False) (· = .statusKey vid)
                    (copyRuleProg (E := E) vid pi [rd1] [wr1]) ιd T := by
                  unfold copyRuleProg
                  dsimp only []
                  apply ExecAvoids.bind_op (readOnly_avoids_status (writes_resolveRead_nil vid pi rd1 ιd T))
                  rw [hp, after_resolveRead]
                  apply ExecAvoids.bind_op (readOnly_avoids_status (writes_resolveWrite_nil vid pi wr1 _ T))
                  rw [hq, after_resolveWrite]
                  apply ExecAvoids.rd_bind (fun h => h)
                  rw [hv]
                  exact ExecAvoids.wr_bind (put_avoids_status hqNotSt val)
                    (ExecAvoids.wr_bind (put_avoids_status hPcNotStatus _) (ExecAvoids.ret _ _ _))
                refine ⟨?_, Or.inl (by simp), Or.inl ?_⟩
                · rw [after_bind_op, hp, after_resolveRead]; dsimp only []
                  rw [after_bind_op, hq, after_resolveWrite]; dsimp only []
                  simp [hv, Prog.bind_eq, Prog.rd_bind, after_read, Prog.wr_bind, after_write,
                    Write.apply, Write.affects, Write.newVal]
                · exact havSt.not_touches rfl
        | [], _, _, hFst => simp at hFst
        | _ :: _ :: _, _, _, hFst => simp at hFst
        | [_], [], _, hFst => simp at hFst
        | [_], _ :: _ :: _, _, hFst => simp at hFst
      · simp only [h1, Bool.false_eq_true, ite_false] at hFst ⊢
        by_cases h2 : (dec.op == "goto") = true
        · simp only [h2, ite_true] at hFst ⊢
          unfold gotoRuleProg at hFst ⊢
          match dec.rdOps, dec.wrOps, hFst with
          | [rd1], [], hFst =>
            dsimp only [] at hFst ⊢
            rw [exec_bind_fst, after_resolveRowTarget_eq] at hFst
            cases hrt : (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) ιd T).1 with
            | none => rw [hrt] at hFst; simp at hFst
            | some n =>
              rw [hrt] at hFst; dsimp only [] at hFst
              simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hFst
              simp at hFst
              rcases hFst with ⟨rfl, rfl⟩
              have havSt : ExecAvoids (fun _ => False) (· = .statusKey vid)
                  (gotoRuleProg (E := E) vid pi [rd1] []) ιd T := by
                unfold gotoRuleProg
                dsimp only []
                apply ExecAvoids.bind_op (resolveRowTarget_avoids_status vid pi rd1 ιd T)
                rw [hrt, after_resolveRowTarget_eq]
                exact ExecAvoids.wr_bind (put_avoids_status hPcNotStatus _) (ExecAvoids.ret _ _ _)
              refine ⟨?_, Or.inl (by simp), Or.inl (havSt.not_touches rfl)⟩
              rw [after_bind_op, hrt, after_resolveRowTarget_eq]; dsimp only []
              simp [Prog.bind_eq, Prog.wr_bind, after_write, Write.apply, Write.affects, Write.newVal]
          | [], _, hFst => simp at hFst
          | _ :: _ :: _, _, hFst => simp at hFst
          | [_], _ :: _, hFst => simp at hFst
        · simp only [h2, Bool.false_eq_true, ite_false] at hFst ⊢
          by_cases h3 : (dec.op == "br") = true
          · simp only [h3, ite_true] at hFst ⊢
            unfold brRuleProg at hFst ⊢
            match dec.rdOps, dec.wrOps, hFst with
            | [rd1, rd2, rd3], [], hFst =>
              dsimp only [] at hFst ⊢
              rw [exec_bind_fst, after_resolveTruth_eq] at hFst
              cases htr : (exec (resolveTruth (I := I) (E := E) vid pi rd1) ιd T).1 with
              | none => rw [htr] at hFst; simp at hFst
              | some b =>
                rw [htr] at hFst; dsimp only [] at hFst
                rw [exec_bind_fst, after_resolveRowTarget_eq] at hFst
                cases hrt : (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) _ T).1 with
                | none => rw [hrt] at hFst; simp at hFst
                | some n =>
                  rw [hrt] at hFst; dsimp only [] at hFst
                  simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hFst
                  simp at hFst
                  rcases hFst with ⟨rfl, rfl⟩
                  have havSt : ExecAvoids (fun _ => False) (· = .statusKey vid)
                      (brRuleProg (E := E) vid pi [rd1, rd2, rd3] []) ιd T := by
                    unfold brRuleProg
                    dsimp only []
                    apply ExecAvoids.bind_op (resolveTruth_avoids_status vid pi rd1 ιd T)
                    rw [htr, after_resolveTruth_eq]
                    apply ExecAvoids.bind_op (resolveRowTarget_avoids_status vid pi (if b then rd2 else rd3) _ T)
                    rw [hrt, after_resolveRowTarget_eq]
                    exact ExecAvoids.wr_bind (put_avoids_status hPcNotStatus _) (ExecAvoids.ret _ _ _)
                  refine ⟨?_, Or.inl (by simp), Or.inl (havSt.not_touches rfl)⟩
                  rw [after_bind_op, htr, after_resolveTruth_eq]; dsimp only []
                  rw [after_bind_op, hrt, after_resolveRowTarget_eq]; dsimp only []
                  simp [Prog.bind_eq, Prog.wr_bind, after_write, Write.apply, Write.affects, Write.newVal]
            | [], _, hFst => simp at hFst
            | [_], _, hFst => simp at hFst
            | [_, _], _, hFst => simp at hFst
            | _ :: _ :: _ :: _ :: _, _, hFst => simp at hFst
            | [_, _, _], _ :: _, hFst => simp at hFst
          · simp only [h3, Bool.false_eq_true, ite_false] at hFst ⊢
            by_cases h4 : (dec.op == "call") = true
            · simp only [h4, ite_true] at hFst ⊢
              unfold callRuleProg at hFst ⊢
              match dec.rdOps, hFst with
              | [], hFst => simp at hFst
              | fnOp :: argRdOps, hFst =>
                dsimp only [] at hFst ⊢
                rw [exec_bind_fst, after_resolveRead] at hFst
                cases hp : (exec (resolveRead (I := I) (E := E) vid pi fnOp) ιd T).1 with
                | none => rw [hp] at hFst; simp at hFst
                | some pFn =>
                  rw [hp] at hFst; dsimp only [] at hFst
                  simp only [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
                  cases hvFn : T pFn with
                  | none => rw [hvFn] at hFst; simp at hFst
                  | some vFn =>
                    rw [hvFn] at hFst
                    cases vFn <;> try (simp at hFst; done)
                    rename_i callee
                    dsimp only [] at hFst
                    rw [← Prog.bind_eq, exec_bind_fst] at hFst
                    cases hb : (exec (bindChild (I := I) (E := E) vid pi callee argRdOps dec.wrOps) _ T).1 with
                    | error _ => rw [hb] at hFst; simp at hFst
                    | ok u =>
                      cases u
                      rw [hb] at hFst; dsimp only [] at hFst
                      simp only [Prog.wr_bind, exec_write] at hFst
                      simp at hFst
                      rcases hFst with ⟨rfl, rfl⟩
                      have havSt : ExecAvoids (fun _ => False) (· = .statusKey vid)
                          (callRuleProg (E := E) vid pi (fnOp :: argRdOps) dec.wrOps) ιd T := by
                        unfold callRuleProg
                        dsimp only []
                        apply ExecAvoids.bind_op (readOnly_avoids_status (writes_resolveRead_nil vid pi fnOp ιd T))
                        rw [hp, after_resolveRead]
                        apply ExecAvoids.rd_bind (fun h => h)
                        rw [hvFn]
                        apply ExecAvoids.bind_op (bindChild_avoids_status vid pi callee argRdOps dec.wrOps _ T)
                        rw [hb]
                        exact ExecAvoids.wr_bind (put_avoids_status hPcNotStatus _) (ExecAvoids.ret _ _ _)
                      refine ⟨?_, Or.inr ⟨rfl, ?_⟩, Or.inl (havSt.not_touches rfl)⟩
                      · rw [after_bind_op, hp, after_resolveRead]; dsimp only []
                        simp only [Prog.bind_eq, Prog.rd_bind, after_read, hvFn]; try dsimp only []
                        rw [← Prog.bind_eq, after_bind_op, hb]; try dsimp only []
                        simp [Prog.wr_bind, after_write, Write.apply, Write.affects, Write.newVal]
                      · rw [exec_bind_op]; dsimp only []
                        rw [hp, after_resolveRead, Act.writes_append, writes_resolveRead_nil, List.nil_append]
                        try dsimp only []
                        simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hvFn, Act.writes_read]; try dsimp only []
                        rw [exec_bind]; dsimp only []
                        rw [hb, Act.writes_append]
                        exact List.mem_append_left _ (bindChild_has_delWhere hb)
            · simp only [h4, Bool.false_eq_true, ite_false] at hFst ⊢
              by_cases h5 : (dec.op == "return") = true
              · simp only [h5, ite_true] at hFst ⊢
                unfold returnRuleProg at hFst ⊢
                match dec.rdOps, dec.wrOps, hFst with
                | [], [], hFst =>
                  dsimp only [] at hFst ⊢
                  by_cases hd : 1 < pi.depth
                  · simp only [hd, ite_true] at hFst ⊢
                    rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
                    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read, Act.writes_read]
                    cases hvRet : T (.frameRetPc vid pi.depth) with
                    | none => rw [hvRet] at hFst; simp at hFst
                    | some vRet =>
                      rw [hvRet] at hFst
                      cases vRet <;> try (simp at hFst; done)
                      rename_i r
                      dsimp only [] at hFst ⊢
                      cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) with
                      | false => rw [hrv] at hFst; simp at hFst
                      | true =>
                        rw [hrv] at hFst
                        simp only [ite_true, Prog.bind_eq, Prog.wr_bind, exec_write] at hFst ⊢
                        simp at hFst
                        rcases hFst with ⟨rfl, rfl⟩
                        have hrdEq : r.depth + 1 = pi.depth := by
                          simp only [Bool.and_eq_true, decide_eq_true_eq] at hrv
                          exact hrv.2
                        refine ⟨?_, Or.inl (by omega), Or.inl ?_⟩
                        · simp [Write.apply, Write.affects, Write.newVal, inFrame]
                        · simp [Write.affects, inFrame]
                  · simp only [hd, ite_false, Prog.bind_eq, Prog.wr_bind, exec_write] at hFst ⊢
                    simp at hFst
                    rcases hFst with ⟨rfl, rfl⟩
                    refine ⟨?_, Or.inl (Nat.le_refl _), Or.inr ⟨.ok, ?_, by decide⟩⟩
                    · simp [Write.apply, Write.affects, hpc]
                    · simp [Write.apply, Write.affects, Write.newVal]
                | _ :: _, _, hFst => simp at hFst
                | [], _ :: _, hFst => simp at hFst
              · simp only [h5, Bool.false_eq_true, ite_false] at hFst ⊢
                by_cases h6 : R.caps dec.op = true
                · simp only [h6, ite_true] at hFst ⊢
                  have hH1 := hR.nativeH1 dec.op h6
                  have hNatFst := hFst
                  unfold nativeRuleProg at hNatFst
                  obtain ⟨_, _, hqsWr⟩ := resolveAllWrite_avoids (I := I) (E := E) hwr
                    (shiftInputs (resolveAllRead (I := I) (E := E) vid pi dec.rdOps) ιd T)
                  rw [exec_bind_fst, after_resolveAllRead_eq] at hNatFst
                  cases hrdK : (exec (resolveAllRead (I := I) (E := E) vid pi dec.rdOps) ιd T).1 with
                  | none => rw [hrdK] at hNatFst; simp at hNatFst
                  | some rdKeys =>
                    rw [hrdK] at hNatFst; dsimp only [] at hNatFst
                    rw [exec_bind_fst, after_resolveAllWrite_eq] at hNatFst
                    cases hwrK : (exec (resolveAllWrite (I := I) (E := E) vid pi dec.wrOps) _ T).1 with
                    | none => rw [hwrK] at hNatFst; simp at hNatFst
                    | some wrKeys =>
                      rw [hwrK] at hNatFst; dsimp only [] at hNatFst
                      rw [exec_bind_fst, after_readKeyList_eq] at hNatFst
                      rw [exec_bind_fst, after_recvMany_eq] at hNatFst
                      cases hEv : (R.native dec.op).eval wrKeys _ _ with
                      | error _ => rw [hEv] at hNatFst; simp at hNatFst
                      | ok out =>
                        rw [hEv] at hNatFst; dsimp only [] at hNatFst
                        rw [exec_bind_fst, after_emitAll_eq, exec_bind_fst] at hNatFst
                        obtain ⟨hSub, hStNoErr⟩ := hH1 wrKeys _ _ out hEv
                        have hOutWr : ∀ k v, (k, v) ∈ out.writes → ForbiddenKey vid pi.depth k = false :=
                          fun k v hkv => (hqsWr wrKeys hwrK k (hSub k v hkv)).1
                        cases hSetSt : out.setStatus with
                        | none =>
                          rw [hSetSt] at hNatFst
                          simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hNatFst
                          simp at hNatFst
                          rcases hNatFst with ⟨rfl, rfl⟩
                          have havSt : ExecAvoids (fun _ => False) (· = .statusKey vid)
                              (nativeRuleProg (R.native dec.op) vid pi dec.rdOps dec.wrOps) ιd T := by
                            unfold nativeRuleProg
                            apply ExecAvoids.bind_op (resolveAllRead_avoids_status vid pi dec.rdOps ιd T)
                            rw [hrdK, after_resolveAllRead_eq]
                            apply ExecAvoids.bind_op (resolveAllWrite_avoids_status vid pi dec.wrOps _ T)
                            rw [hwrK, after_resolveAllWrite_eq]
                            apply ExecAvoids.bind_op (readKeyList_avoids_status vid rdKeys _ T)
                            rw [after_readKeyList_eq]
                            apply ExecAvoids.bind_op (recvMany_avoids_status vid (R.native dec.op).numInputs _ T)
                            rw [after_recvMany_eq, hEv]
                            apply ExecAvoids.bind_op (emitAll_avoids_status vid out.events _ T)
                            rw [after_emitAll_eq]
                            apply ExecAvoids.bind_op (putAll_avoids_status hOutWr _ T)
                            apply ExecAvoids.wr_bind (put_avoids_status hPcNotStatus _)
                            rw [hSetSt]; exact ExecAvoids.ret _ _ _
                          refine ⟨?_, Or.inl (by simp), Or.inl (havSt.not_touches rfl)⟩
                          unfold nativeRuleProg
                          rw [after_bind_op, hrdK, after_resolveAllRead_eq]; dsimp only []
                          rw [after_bind_op, hwrK, after_resolveAllWrite_eq]; dsimp only []
                          rw [after_bind_op, after_readKeyList_eq, after_bind_op, after_recvMany_eq, hEv]
                          dsimp only []
                          rw [after_bind_op, after_emitAll_eq, after_bind_op]
                          simp [hSetSt, Prog.bind_eq, Prog.wr_bind, after_write,
                            Write.apply, Write.affects, Write.newVal]
                        | some s =>
                          rw [hSetSt] at hNatFst
                          simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hNatFst
                          simp at hNatFst
                          rcases hNatFst with ⟨rfl, rfl⟩
                          refine ⟨?_, Or.inl (by simp), Or.inr ⟨s, ?_, hStNoErr s hSetSt⟩⟩
                          · unfold nativeRuleProg
                            rw [after_bind_op, hrdK, after_resolveAllRead_eq]; dsimp only []
                            rw [after_bind_op, hwrK, after_resolveAllWrite_eq]; dsimp only []
                            rw [after_bind_op, after_readKeyList_eq, after_bind_op, after_recvMany_eq, hEv]
                            dsimp only []
                            rw [after_bind_op, after_emitAll_eq, after_bind_op]
                            simp [hSetSt, Prog.bind_eq, Prog.wr_bind, after_write, Write.apply, Write.affects, Write.newVal]
                          · unfold nativeRuleProg
                            rw [after_bind_op, hrdK, after_resolveAllRead_eq]; dsimp only []
                            rw [after_bind_op, hwrK, after_resolveAllWrite_eq]; dsimp only []
                            rw [after_bind_op, after_readKeyList_eq, after_bind_op, after_recvMany_eq, hEv]
                            dsimp only []
                            rw [after_bind_op, after_emitAll_eq, after_bind_op]
                            simp [hSetSt, Prog.bind_eq, Prog.wr_bind, after_write, Write.apply, Write.affects, Write.newVal]
                · simp only [h6, Bool.false_eq_true, ite_false] at hFst ⊢
                  have hQNotSt : Key.queueEntry dec.op vid ≠ .statusKey vid := by intro h; cases h
                  have nonDelegOk : ∀ ιh,
                      (exec (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T).1 = .ok (pi', evs) →
                      after (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T (.pcKey vid) = some (.pc pi') ∧
                      (pi'.depth ≤ pi.depth ∨
                        (pi'.depth = pi.depth + 1 ∧
                         Write.delWhere (inFrame vid (pi.depth + 1)) ∈
                           Act.writes (exec (do
                             let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                             match vHdr with
                             | some (.routeHdr _) =>
                                 handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                             | some .rwfuncHdr =>
                                 userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                             | _ => pure (.error "unmatched_opcode")) ιh T).2)) ∧
                      (touches (Act.writes (exec (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T).2) (.statusKey vid) = false ∨
                        ∃ s', after (do
                          let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                          match vHdr with
                          | some (.routeHdr _) =>
                              handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                          | some .rwfuncHdr =>
                              userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                          | _ => pure (.error "unmatched_opcode")) ιh T (.statusKey vid) = some (.status s') ∧
                          s' ≠ .error) := by
                    intro ιh hOkH
                    rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hOkH
                    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read, Act.writes_read]
                    cases hvHdr : T (.libHeader dec.op) with
                    | none => rw [hvHdr] at hOkH; simp at hOkH
                    | some vHdr =>
                      rw [hvHdr] at hOkH
                      cases vHdr <;> try (simp at hOkH; done)
                      · dsimp only [] at hOkH ⊢
                        have hUc := hOkH
                        unfold userCallRuleProg at hUc
                        rw [exec_bind_fst] at hUc
                        cases hb : (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ιh T).1 with
                        | error _ => rw [hb] at hUc; simp at hUc
                        | ok u =>
                          cases u; rw [hb] at hUc; dsimp only [] at hUc
                          simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hUc
                          simp at hUc
                          rcases hUc with ⟨rfl, rfl⟩
                          have havSt : ExecAvoids (fun _ => False) (· = .statusKey vid)
                              (userCallRuleProg (E := E) dec.op vid pi dec.rdOps dec.wrOps) ιh T := by
                            unfold userCallRuleProg
                            apply ExecAvoids.bind_op (bindChild_avoids_status vid pi dec.op dec.rdOps dec.wrOps ιh T)
                            rw [hb]
                            exact ExecAvoids.wr_bind (put_avoids_status hPcNotStatus _) (ExecAvoids.ret _ _ _)
                          refine ⟨?_, Or.inr ⟨rfl, ?_⟩, Or.inl (havSt.not_touches rfl)⟩
                          · unfold userCallRuleProg
                            rw [after_bind_op, hb]; dsimp only []
                            simp [Prog.bind_eq, Prog.wr_bind, after_write, Write.apply, Write.affects, Write.newVal]
                          · unfold userCallRuleProg
                            rw [exec_bind_op]; dsimp only []
                            rw [hb, Act.writes_append]
                            exact List.mem_append_left _ (bindChild_has_delWhere hb)
                      · dsimp only [] at hOkH ⊢
                        have hExt := hR.handoffH2 dec.op
                        have hHand := hOkH
                        unfold handoffRuleProg at hHand
                        rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hHand
                        cases hq : T (.queueEntry dec.op vid) with
                        | some _ => rw [hq] at hHand; simp at hHand
                        | none =>
                          rw [hq] at hHand
                          dsimp only [] at hHand
                          simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hHand
                          rw [← Prog.bind_eq, exec_bind_fst, after_ext_eq hExt] at hHand
                          cases hex : (exec ((R.handoffExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                          | error _ => rw [hex] at hHand; simp at hHand
                          | ok out =>
                            rw [hex] at hHand; dsimp only [] at hHand
                            rw [← Prog.bind_eq, exec_bind_fst, after_emitAll_eq, ← Prog.bind_eq, exec_bind_fst] at hHand
                            simp at hHand
                            rcases hHand with ⟨rfl, rfl⟩
                            have hPcV := hExt.pcValid vid pi dec.rdOps dec.wrOps _ _ out hpv hex
                            have hOutWr : ∀ k v, (k, v) ∈ out.writes → ForbiddenKey vid pi.depth k = false :=
                              fun k v hkv => (hExt.writesSafe vid pi dec.rdOps dec.wrOps _ _ out k v hex hkv).1
                            have havSt : ExecAvoids (fun _ => False) (· = .statusKey vid)
                                (handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps) ιh T := by
                              unfold handoffRuleProg
                              apply ExecAvoids.rd_bind (fun h => h)
                              rw [hq]; dsimp only []
                              apply ExecAvoids.wr_bind (put_avoids_status hQNotSt _)
                              apply ExecAvoids.bind_op (readOnly_avoids_status (hExt.internalPure vid pi dec.rdOps dec.wrOps _ _).1)
                              rw [hex, after_ext_eq hExt]; dsimp only []
                              apply ExecAvoids.bind_op (emitAll_avoids_status vid out.events _ _)
                              rw [after_emitAll_eq]
                              apply ExecAvoids.bind_op (putAll_avoids_status hOutWr _ _)
                              apply ExecAvoids.wr_bind (put_avoids_status hPcNotStatus _)
                              exact ExecAvoids.wr_bind (del_avoids_status hQNotSt) (ExecAvoids.ret _ _ _)
                            refine ⟨?_, Or.inl (by omega), Or.inl (havSt.not_touches rfl)⟩
                            unfold handoffRuleProg
                            simp only [Prog.bind_eq, Prog.rd_bind, after_read, hq]; try dsimp only []
                            simp only [Prog.wr_bind, after_write]
                            rw [after_bind, hex, after_ext_eq hExt]; try dsimp only []
                            rw [after_bind, after_emitAll_eq, after_bind]
                            simp [after_write, Write.apply, Write.affects, Write.newVal]
                  rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
                  rw [Prog.bind_eq, Prog.rd_bind, after_read]
                  rw [Prog.bind_eq, Prog.rd_bind, exec_read, Act.writes_read]
                  cases hvDel : T (.delegEntry dec.op) with
                  | none =>
                    rw [hvDel] at hFst
                    exact nonDelegOk _ hFst
                  | some vDel =>
                    rw [hvDel] at hFst
                    cases vDel <;> try exact nonDelegOk _ hFst
                    dsimp only [] at hFst ⊢
                    have hExt := hR.delegH2 dec.op
                    unfold delegRuleProg at hFst ⊢
                    rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
                    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read, Act.writes_read]
                    cases hq : T (.queueEntry dec.op vid) with
                    | some _ => rw [hq] at hFst; simp at hFst
                    | none =>
                      rw [hq] at hFst
                      dsimp only [] at hFst ⊢
                      simp only [Prog.bind_eq, Prog.wr_bind, exec_write, after_write] at hFst ⊢
                      rw [← Prog.bind_eq, exec_bind_fst, after_ext_eq hExt] at hFst
                      cases hex : (exec ((R.delegExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                      | error _ => rw [hex] at hFst; simp at hFst
                      | ok out =>
                        rw [hex] at hFst; dsimp only [] at hFst
                        rw [← Prog.bind_eq, exec_bind_fst, after_emitAll_eq, ← Prog.bind_eq, exec_bind_fst] at hFst
                        simp at hFst
                        rcases hFst with ⟨rfl, rfl⟩
                        have hPcV := hExt.pcValid vid pi dec.rdOps dec.wrOps _ _ out hpv hex
                        rw [after_bind, hex, after_ext_eq hExt]; dsimp only []
                        rw [after_bind, after_emitAll_eq, after_bind]
                        refine ⟨?_, Or.inl (by omega), Or.inr ⟨.running, ?_, by decide⟩⟩
                        · simp [after_write, Write.apply, Write.affects, Write.newVal]
                        · simp [after_write, Write.apply, Write.affects, Write.newVal]

private theorem isDeadFrameKey_antitone {vid : Vid} {d₁ d₂ : Nat} {k : Key}
    (hle : d₁ ≤ d₂) (h : isDeadFrameKey vid d₂ k = true) :
    isDeadFrameKey vid d₁ k = true := by
  cases k <;> simp only [isDeadFrameKey, Bool.false_eq_true, decide_eq_true_eq] at h ⊢ <;>
    (try contradiction) <;>
    rcases h with ⟨rfl, h⟩ <;> exact ⟨rfl, by omega⟩

private theorem inFrame_of_dead_not_dead_succ {vid : Vid} {d : Nat} {k : Key}
    (hd : isDeadFrameKey vid d k = true)
    (hnd : ¬ (isDeadFrameKey vid (d + 1) k = true)) :
    inFrame vid (d + 1) k = true := by
  have hArith : ∀ e, (e = 0 ∨ d < e) → ¬ (e = 0 ∨ d + 1 < e) → e = d + 1 := by omega
  cases k <;>
    simp only [isDeadFrameKey, inFrame, Bool.false_eq_true, decide_eq_true_eq] at hd hnd ⊢ <;>
    (try contradiction) <;>
    rcases hd with ⟨rfl, hd⟩ <;>
    exact ⟨rfl, hArith _ hd (fun h => hnd ⟨rfl, h⟩)⟩

private theorem exec_finalizeStep_indep (vid : Vid) (res : Except ErrMsg (PC × List E))
    (ι₁ ι₂ : Nat → I) (S₁ S₂ : Store Key (Val D)) :
    exec (finalizeStep (E := E) vid res) ι₁ S₁ = exec (finalizeStep vid res) ι₂ S₂ := by
  cases res with
  | ok p => rcases p with ⟨_, _⟩; rfl
  | error _ => rfl

/-- **Lemma 8 (`lem:approx`, `step_respects_agree`)**:
if `T ~ U` (`StoreAgree vid T U`), both `T` and `U` pass the gate (`rho = none`),
and `(P)` (`StepDiscipline vid T`) holds, then `gatedStep` from `T` and `U`
produces the **exact same outcome and ordered action trace**, and reaches
`StoreAgree`-equivalent post-step stores. -/
theorem step_respects_agree {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T U : Store Key (Val D)}
    (hAgree : StoreAgree vid T U)
    (hrhoT : T (.stopKey vid) = none)
    (hrhoU : U (.stopKey vid) = none)
    (hP : StepDiscipline vid T) (ι : Nat → I) :
    exec (gatedStep R vid) ι U = exec (gatedStep R vid) ι T ∧
    StoreAgree vid (after (gatedStep R vid) ι T) (after (gatedStep R vid) ι U) := by
  have hCoreEq := stepCore_exec_congr_agree (E := E) hR hAgree hP ι
  have hExecEq : exec (gatedStep R vid) ι U = exec (gatedStep R vid) ι T := by
    unfold gatedStep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoT, hrhoU]
    try dsimp only []
    rw [exec_bind, exec_bind]
    dsimp only []
    rw [hCoreEq, exec_finalizeStep_indep vid (exec (stepCore R vid) ι T).1 _ _ _ _]
  refine ⟨hExecEq, ?_⟩
  cases hCore : exec (stepCore R vid) ι T with
  | mk res asCore =>
    have hCoreU : exec (stepCore R vid) ι U = (res, asCore) := hCoreEq.trans hCore
    cases res with
    | error err =>
      have hFailT : (exec (stepCore R vid) ι T).1 = .error err := by rw [hCore]
      have hFailU : (exec (stepCore R vid) ι U).1 = .error err := by rw [hCoreU]
      have hAT := failed_gatedStep_preserves_agree hR hrhoT hFailT
      have hAU := failed_gatedStep_preserves_agree hR hrhoU hFailU
      exact hAT.trans (hAgree.trans hAU.symm)
    | ok pair =>
      rcases pair with ⟨pi', evs⟩
      obtain ⟨pi, hpcT, _, hpcAfterT, hDepth, _⟩ := stepCore_ok_post hR hP hCore
      let ws := Act.writes asCore
      have hAfterT : after (gatedStep R vid) ι T = applyAll T ws := by
        unfold gatedStep
        simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoT]
        rw [after_bind]
        unfold after at *
        rw [hCore]
        rfl
      have hAfterU : after (gatedStep R vid) ι U = applyAll U ws := by
        unfold gatedStep
        simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoU]
        rw [after_bind]
        unfold after at *
        rw [hCoreU]
        rfl
      rw [hAfterT, hAfterU]
      have hpcEqAfter : applyAll T ws (.pcKey vid) = applyAll U ws (.pcKey vid) := by
        cases ht : touches ws (.pcKey vid) with
        | true => exact applyAll_eq_of_touches T U ht
        | false =>
          rw [applyAll_of_not_touches ht, applyAll_of_not_touches ht]
          exact hAgree.1
      refine ⟨hpcEqAfter, ?_⟩
      intro k hk
      have hdAfter : liveDepthOf vid (applyAll T ws) = pi'.depth :=
        liveDepthOf_pc hpcAfterT
      rw [hdAfter] at hk
      cases ht : touches ws k with
      | true => exact applyAll_eq_of_touches T U ht
      | false =>
        rw [applyAll_of_not_touches ht, applyAll_of_not_touches ht]
        apply hAgree.2 k
        rw [liveDepthOf_pc hpcT]
        rintro (rfl | rfl | rfl | rfl | hdead)
        · exact hk (Or.inl rfl)
        · exact hk (Or.inr (Or.inl rfl))
        · exact hk (Or.inr (Or.inr (Or.inl rfl)))
        · exact hk (Or.inr (Or.inr (Or.inr (Or.inl rfl))))
        · rcases hDepth with hle | ⟨hd1, hdelMem⟩
          · exact hk (Or.inr (Or.inr (Or.inr (Or.inr (isDeadFrameKey_antitone hle hdead)))))
          · have hnotDead' : ¬ (isDeadFrameKey vid (pi.depth + 1) k = true) := by
              rw [← hd1]
              exact fun hd' => hk (Or.inr (Or.inr (Or.inr (Or.inr hd'))))
            have hinChild := inFrame_of_dead_not_dead_succ hdead hnotDead'
            have htTrue := touches_of_mem hdelMem (by simpa [Write.affects] using hinChild)
            rw [htTrue] at ht
            cases ht

end StepRespectsAgree

/-! ## Uninterrupted Commit Sequence and Theorem 3 (`thm:resumption`) -/

section Resumption

variable {D I E : Type}

/-- Boolean test matching `StoreRunnable vid T`. -/
def isStoreRunnableBool (vid : Vid) (T : Store Key (Val D)) : Bool :=
  match T (.statusKey vid) with
  | some (.status s) => s.isRun
  | _ => false

theorem isStoreRunnableBool_iff (vid : Vid) (T : Store Key (Val D)) :
    isStoreRunnableBool vid T = true ↔ StoreRunnable vid T := by
  unfold isStoreRunnableBool StoreRunnable
  cases T (.statusKey vid) with
  | none => simp
  | some v =>
    cases v <;> simp

/-- Initial resume normalization (`05-resumption.tex` line 40): resumes `T₀`
if `T₀` is in `paused` or `error`. -/
def initCommit (vid : Vid) (T₀ : Store Key (Val D)) : Store Key (Val D) :=
  match T₀ (.statusKey vid) with
  | some (.status s) => if s.isResumable then resumeStore vid T₀ else T₀
  | _ => T₀

/-- Self-pause normalization (`13-appendix.tex` line 399): immediately resumes
any step whose handler wrote `sigma := paused`. -/
def autoResumePaused (vid : Vid) (T : Store Key (Val D)) : Store Key (Val D) :=
  match T (.statusKey vid) with
  | some (.status .paused) => resumeStore vid T
  | _ => T

theorem storeAgree_initCommit (vid : Vid) (T₀ : Store Key (Val D)) :
    StoreAgree vid (initCommit vid T₀) T₀ := by
  unfold initCommit
  cases T₀ (.statusKey vid) with
  | none => exact StoreAgree.refl vid T₀
  | some v =>
    cases v <;> try exact StoreAgree.refl vid T₀
    rename_i s
    dsimp only []
    cases s.isResumable
    · exact StoreAgree.refl vid T₀
    · exact storeAgree_resumeStore vid T₀

theorem storeAgree_autoResumePaused (vid : Vid) (T : Store Key (Val D)) :
    StoreAgree vid (autoResumePaused vid T) T := by
  unfold autoResumePaused
  cases T (.statusKey vid) with
  | none => exact StoreAgree.refl vid T
  | some v =>
    cases v <;> try exact StoreAgree.refl vid T
    rename_i s
    cases s <;> first | exact StoreAgree.refl vid T | exact storeAgree_resumeStore vid T

/-- Well-formed starting commit point `T₀` (`05-resumption.tex` lines 8-9, 39):
runnable with no pending stop, stopped in `{paused, error}`, or terminal `ok`. -/
def ValidCommitPoint (vid : Vid) (T₀ : Store Key (Val D)) : Prop :=
  (StoreRunnable vid T₀ ∧ T₀ (.stopKey vid) = none) ∨
  (∃ s, T₀ (.statusKey vid) = some (.status s) ∧ s.isResumable = true) ∨
  T₀ (.statusKey vid) = some (.status .ok)

/-- Uninterrupted commit point `T_k` before the `(k+1)`-th step under step-indexed
external inputs `ιs` (`none` if the uninterrupted run halted before `k` labels). -/
def commitPointAt (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) : Nat → Option (Store Key (Val D))
  | 0 => some (initCommit vid T₀)
  | k + 1 =>
      match commitPointAt R vid ιs T₀ k with
      | none => none
      | some Tk =>
          if isStoreRunnableBool vid Tk then
            match (exec (gatedStep R vid) (ιs k) Tk).1 with
            | .stepped _ _ => some (autoResumePaused vid (after (gatedStep R vid) (ιs k) Tk))
            | _ => none
          else
            none

/-- The `(k+1)`-th observable label `ℓ_{k+1}` (0-indexed at `k`) of the
uninterrupted run from `T₀`, or `none` if the run has fewer than `k + 1` labels. -/
def uninterruptedLabelAt (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) (k : Nat) : Option (StepLabel E) :=
  match commitPointAt R vid ιs T₀ k with
  | none => none
  | some Tk =>
      if isStoreRunnableBool vid Tk then
        (exec (gatedStep R vid) (ιs k) Tk).1.label?
      else
        none

/-- Prefix `ℓ_1 ... ℓ_n` of the uninterrupted trace up to `n` step attempts. -/
def uninterruptedPrefix (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) : Nat → List (StepLabel E)
  | 0 => []
  | k + 1 =>
      match uninterruptedLabelAt R vid ιs T₀ k with
      | none => uninterruptedPrefix R vid ιs T₀ k
      | some lbl => uninterruptedPrefix R vid ιs T₀ k ++ [lbl]

theorem uninterruptedPrefix_mono (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) {k N : Nat} (hle : k ≤ N) :
    uninterruptedPrefix R vid ιs T₀ k <+: uninterruptedPrefix R vid ιs T₀ N := by
  induction N with
  | zero =>
    have hk0 : k = 0 := by omega
    subst hk0
    exact List.prefix_refl _
  | succ N ih =>
    rcases Nat.eq_or_lt_of_le hle with rfl | hlt
    · exact List.prefix_refl _
    · have hprev := ih (by omega)
      simp only [uninterruptedPrefix]
      cases uninterruptedLabelAt R vid ιs T₀ N with
      | none => exact hprev
      | some lbl => exact hprev.trans (List.prefix_append _ [lbl])

theorem commitPointAt_none_succ (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) {n : Nat}
    (h : uninterruptedLabelAt R vid ιs T₀ n = none) :
    commitPointAt R vid ιs T₀ (n + 1) = none := by
  unfold uninterruptedLabelAt at h
  simp only [commitPointAt]
  cases hcp : commitPointAt R vid ιs T₀ n with
  | none => rfl
  | some Tn =>
    rw [hcp] at h
    dsimp only [] at h ⊢
    cases hrun : isStoreRunnableBool vid Tn with
    | false => rfl
    | true =>
      simp only [hrun, ite_true] at h ⊢
      cases hout : (exec (gatedStep R vid) (ιs n) Tn).1 <;>
        simp [hout, StepOutcome.label?] at h ⊢

theorem commitPointAt_none_mono (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) {n m : Nat}
    (h : commitPointAt R vid ιs T₀ n = none) (hle : n ≤ m) :
    commitPointAt R vid ιs T₀ m = none := by
  induction m with
  | zero =>
    have hn0 : n = 0 := by omega
    subst hn0; exact h
  | succ m ih =>
    rcases Nat.eq_or_lt_of_le hle with rfl | hlt
    · exact h
    · simp [commitPointAt, ih (by omega)]

theorem uninterruptedLabelAt_none_mono (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) {n m : Nat}
    (h : uninterruptedLabelAt R vid ιs T₀ n = none) (hle : n ≤ m) :
    uninterruptedLabelAt R vid ιs T₀ m = none := by
  rcases Nat.eq_or_lt_of_le hle with rfl | hlt
  · exact h
  · have hcNone := commitPointAt_none_mono R vid ιs T₀
      (commitPointAt_none_succ R vid ιs T₀ h) (by omega : n + 1 ≤ m)
    simp [uninterruptedLabelAt, hcNone]

/-- Embedding any deterministic `exec P ι T` into `ValidActionExec` with zero stops. -/
theorem validActionExec_of_exec {α : Type} (vid : Vid)
    (P : Prog Key (Val D) I E α) (ι : Nat → I) (T : Store Key (Val D)) :
    ValidActionExec vid P ι T ((exec P ι T).2.map .drv) (exec P ι T).1 (after P ι T) := by
  induction P generalizing ι T with
  | ret a => exact ValidActionExec.ret a ι T
  | read k next ih => exact ValidActionExec.read (ih (T k) ι T)
  | write w next ih => exact ValidActionExec.write (ih ι (w.apply T))
  | input next ih => exact ValidActionExec.input (ih (ι 0) (fun n => ι (n + 1)) T)
  | emit e next ih => exact ValidActionExec.emit (ih ι T)

/-- Full v2 execution `ρ` from `T₀` under step-indexed external inputs `ιs`:
includes arbitrary environment `stop`s (both between transitions and mid-step
inside `ValidActionExec` or mid-`resume`), `createIfAbsent` lease acquisitions,
lease releases, `Pause` transitions, and `resume` transitions. -/
inductive ResumableRun (R : Runtime D I E) (vid : Vid) (ιs : Nat → Nat → I)
    (T₀ : Store Key (Val D)) : List (StepLabel E) → Store Key (Val D) → Prop where
  | init :
      ResumableRun R vid ιs T₀ [] T₀
  | stop {tr : List (StepLabel E)} {S : Store Key (Val D)} :
      ResumableRun R vid ιs T₀ tr S →
      ResumableRun R vid ιs T₀ tr ((stopWrite vid).apply S)
  | leaseAcquire {tr : List (StepLabel E)} {S : Store Key (Val D)} (tok : Token) :
      ResumableRun R vid ιs T₀ tr S →
      ResumableRun R vid ιs T₀ tr (createIfAbsent (.driverKey vid) (.lease tok) S).2
  | leaseRelease {tr : List (StepLabel E)} {S : Store Key (Val D)} :
      ResumableRun R vid ιs T₀ tr S →
      ResumableRun R vid ιs T₀ tr ((Write.del (.driverKey vid)).apply S)
  | resume {tr : List (StepLabel E)} {S : Store Key (Val D)} (midStops : Nat) :
      ResumableRun R vid ιs T₀ tr S →
      ResumeEnabled vid S →
      ResumableRun R vid ιs T₀ tr
        ((Write.put (.statusKey vid) (.status .running)).apply
          (applyStops vid midStops ((Write.del (.stopKey vid)).apply S)))
  | gatedInFlight {tr : List (StepLabel E)} {S S' : Store Key (Val D)}
      {ι : Nat → I} {mas : List (MicroAct D I E)} {out : StepOutcome E} :
      ResumableRun R vid ιs T₀ tr S →
      StoreRunnable vid S →
      (S (.stopKey vid) = none → ι = ιs tr.length) →
      ValidActionExec vid (gatedContinuation R vid (S (.stopKey vid))) ι S mas out S' →
      ResumableRun R vid ιs T₀
        (match out.label? with
         | some lbl => tr ++ [lbl]
         | none => tr)
        S'

/-- Coarse step-boundary `resume` as a special case (`midStops = 0`) of `ResumableRun`. -/
theorem ResumableRun.resumeBoundary {R : Runtime D I E} {vid : Vid}
    {ιs : Nat → Nat → I} {T₀ S : Store Key (Val D)} {tr : List (StepLabel E)}
    (hRun : ResumableRun R vid ιs T₀ tr S) (hEn : ResumeEnabled vid S) :
    ResumableRun R vid ιs T₀ tr (resumeStore vid S) :=
  ResumableRun.resume 0 hRun hEn

/-- Coarse step-boundary `gatedStep` as a special case (zero mid-step stops) of `ResumableRun`. -/
theorem ResumableRun.gatedBoundary {R : Runtime D I E} {vid : Vid}
    {ιs : Nat → Nat → I} {T₀ S : Store Key (Val D)} {tr : List (StepLabel E)}
    {ι : Nat → I} (hRun : ResumableRun R vid ιs T₀ tr S)
    (hRunnable : StoreRunnable vid S)
    (hInput : S (.stopKey vid) = none → ι = ιs tr.length) :
    ResumableRun R vid ιs T₀
      (match (exec (gatedStep R vid) ι S).1.label? with
       | some lbl => tr ++ [lbl]
       | none => tr)
      (after (gatedStep R vid) ι S) := by
  have hexec := validActionExec_of_exec vid
    (gatedContinuation R vid (S (.stopKey vid))) ι S
  have houtEq : (exec (gatedContinuation R vid (S (.stopKey vid))) ι S).1 =
      (exec (gatedStep R vid) ι S).1 := by
    rw [gatedStep_eq_read_continuation]; rfl
  have hafEq : after (gatedContinuation R vid (S (.stopKey vid))) ι S =
      after (gatedStep R vid) ι S := by
    rw [gatedStep_eq_read_continuation]; rfl
  rw [← houtEq, ← hafEq]
  exact ResumableRun.gatedInFlight hRun hRunnable hInput hexec

/-- Status invariant relating an interrupted run's store `S` to commit point `Tk`:
either `Tk` is runnable with `rho = none` and `S` has a non-`ok` status, or both
`Tk` and `S` hold terminal `ok`. -/
def StatusRel (vid : Vid) (S Tk : Store Key (Val D)) : Prop :=
  (StoreRunnable vid Tk ∧ Tk (.stopKey vid) = none ∧
    ∃ s, S (.statusKey vid) = some (.status s) ∧ s ≠ .ok) ∨
  (Tk (.statusKey vid) = some (.status .ok) ∧
    S (.statusKey vid) = some (.status .ok))

private theorem resumeStore_status_and_rho (vid : Vid) (T : Store Key (Val D)) :
    resumeStore vid T (.statusKey vid) = some (.status .running) ∧
    resumeStore vid T (.stopKey vid) = none := by
  unfold resumeStore
  simp [Write.apply, Write.affects, Write.newVal]

private theorem statusRel_preserve_unchanged {vid : Vid} {S S' Tk : Store Key (Val D)}
    (hRel : StatusRel vid S Tk) (hSt : S' (.statusKey vid) = S (.statusKey vid)) :
    StatusRel vid S' Tk := by
  rcases hRel with ⟨hRun, hRho, s, hs, hne⟩ | ⟨hOkT, hOkS⟩
  · exact Or.inl ⟨hRun, hRho, s, hSt.trans hs, hne⟩
  · exact Or.inr ⟨hOkT, hSt.trans hOkS⟩

/-- **Theorem 3 (`thm:resumption`, `stop_resume_resumption`)**:
for any valid commit point `T₀` whose uninterrupted commit sequence satisfies `(P)`
(`StepDiscipline`), every run `ResumableRun R vid ιs T₀ tr S` (with arbitrary
mid-step or boundary `stop`s, `Pause`s, `createIfAbsent` lease acquisitions,
lease releases, and `resume`s) satisfies:
1. `tr = uninterruptedPrefix R vid ιs T₀ tr.length` and every `i < tr.length`
   matches `uninterruptedLabelAt R vid ιs T₀ i = some tr[i]`;
2. `commitPointAt R vid ιs T₀ tr.length = some Tk` for a commit point `Tk`
   with `StoreAgree vid S Tk` (`S ~ Tk`) and `StatusRel vid S Tk`. -/
theorem stop_resume_resumption {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ : Store Key (Val D)}
    (hInit : ValidCommitPoint vid T₀)
    (hP : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    {tr : List (StepLabel E)} {S : Store Key (Val D)}
    (hRun : ResumableRun R vid ιs T₀ tr S) :
    tr = uninterruptedPrefix R vid ιs T₀ tr.length ∧
    (∀ i (hi : i < tr.length), uninterruptedLabelAt R vid ιs T₀ i = some tr[i]) ∧
    ∃ Tk, commitPointAt R vid ιs T₀ tr.length = some Tk ∧
      StoreAgree vid S Tk ∧ StatusRel vid S Tk := by
  induction hRun with
  | init =>
    refine ⟨rfl, by intro i hi; simp at hi, initCommit vid T₀, rfl,
      (storeAgree_initCommit vid T₀).symm, ?_⟩
    rcases hInit with ⟨⟨s, hs, hrun⟩, hrho⟩ | ⟨s, hs, hres⟩ | hok
    · have hnotRes : s.isResumable = false := by cases s <;> simp_all [Status.isRun, Status.isResumable]
      have hsNeOk : s ≠ .ok := by rintro rfl; simp at hrun
      have hEq : initCommit vid T₀ = T₀ := by simp [initCommit, hs, hnotRes]
      rw [hEq]
      exact Or.inl ⟨⟨s, hs, hrun⟩, hrho, s, hs, hsNeOk⟩
    · have hsNeOk : s ≠ .ok := by rintro rfl; simp at hres
      have hEq : initCommit vid T₀ = resumeStore vid T₀ := by simp [initCommit, hs, hres]
      rw [hEq]
      obtain ⟨hStR, hRhoR⟩ := resumeStore_status_and_rho vid T₀
      exact Or.inl ⟨⟨.running, hStR, rfl⟩, hRhoR, s, hs, hsNeOk⟩
    · have hEq : initCommit vid T₀ = T₀ := by simp [initCommit, hok]
      rw [hEq]
      exact Or.inr ⟨hok, hok⟩
  | @stop tr S _ ih =>
    obtain ⟨htr, hidx, Tk, hcp, hAg, hRel⟩ := ih
    have hSt : (stopWrite vid).apply S (.statusKey vid) = S (.statusKey vid) := by
      simp [stopWrite, Write.apply, Write.affects]
    exact ⟨htr, hidx, Tk, hcp, (storeAgree_stopWrite vid S).trans hAg,
      statusRel_preserve_unchanged hRel hSt⟩
  | @leaseAcquire tr S tok _ ih =>
    obtain ⟨htr, hidx, Tk, hcp, hAg, hRel⟩ := ih
    cases hDrv : S (.driverKey vid) with
    | none =>
      rw [createIfAbsent_absent _ hDrv]
      have hSt : (Write.put (.driverKey vid) (.lease tok)).apply S (.statusKey vid) =
          S (.statusKey vid) := by simp [Write.apply, Write.affects]
      exact ⟨htr, hidx, Tk, hcp, (storeAgree_leasePut vid tok S).trans hAg,
        statusRel_preserve_unchanged hRel hSt⟩
    | some v =>
      rw [createIfAbsent_present _ v hDrv]
      exact ⟨htr, hidx, Tk, hcp, hAg, hRel⟩
  | @leaseRelease tr S _ ih =>
    obtain ⟨htr, hidx, Tk, hcp, hAg, hRel⟩ := ih
    have hSt : (Write.del (.driverKey vid)).apply S (.statusKey vid) = S (.statusKey vid) := by
      simp [Write.apply, Write.affects]
    exact ⟨htr, hidx, Tk, hcp, (storeAgree_leaseDel vid S).trans hAg,
      statusRel_preserve_unchanged hRel hSt⟩
  | @resume tr S midStops _ hEn ih =>
    obtain ⟨htr, hidx, Tk, hcp, hAg, hRel⟩ := ih
    obtain ⟨_, sS, hsS, hresS⟩ := hEn
    have hsNeOk : sS ≠ .ok := by rintro rfl; simp at hresS
    have hTkRun : StoreRunnable vid Tk ∧ Tk (.stopKey vid) = none := by
      rcases hRel with ⟨hR_Tk, hRho_Tk, _⟩ | ⟨_, hOkS⟩
      · exact ⟨hR_Tk, hRho_Tk⟩
      · rw [hsS] at hOkS; cases hOkS; exact absurd rfl hsNeOk
    have hComm :
        (Write.put (K := Key) (V := Val D) (.statusKey vid) (.status .running)).apply
          (applyStops vid midStops ((Write.del (.stopKey vid)).apply S)) =
        applyStops vid midStops (resumeStore vid S) := by
      have hw : (Write.put (K := Key) (V := Val D) (.statusKey vid) (.status .running)).affects
          (.stopKey vid) = false := rfl
      simpa [resumeStore] using
        write_commutes_applyStops hw midStops ((Write.del (.stopKey vid)).apply S)
    rw [hComm]
    have hAg' : StoreAgree vid (applyStops vid midStops (resumeStore vid S)) Tk :=
      (storeAgree_applyStops vid midStops _).trans
        ((storeAgree_resumeStore vid S).trans hAg)
    have hSt' : applyStops vid midStops (resumeStore vid S) (.statusKey vid) =
        some (.status .running) := by
      rw [applyStops_other_key vid midStops _ (by intro h; cases h)]
      exact (resumeStore_status_and_rho vid S).1
    exact ⟨htr, hidx, Tk, hcp, hAg',
      Or.inl ⟨hTkRun.1, hTkRun.2, .running, hSt', by decide⟩⟩
  | @gatedInFlight tr S S' ι mas out _ hRunS hIn hexec ih =>
    obtain ⟨htr, hidx, Tk, hcp, hAg, hRel⟩ := ih
    obtain ⟨sS, hsS, hsRun⟩ := hRunS
    have hsNeOk : sS ≠ .ok := by rintro rfl; simp at hsRun
    have hTkRun : StoreRunnable vid Tk ∧ Tk (.stopKey vid) = none := by
      rcases hRel with ⟨hR_Tk, hRho_Tk, _⟩ | ⟨_, hOkS⟩
      · exact ⟨hR_Tk, hRho_Tk⟩
      · rw [hsS] at hOkS; cases hOkS; exact absurd rfl hsNeOk
    have hPTk : StepDiscipline vid Tk := hP tr.length Tk hcp hTkRun.1
    have hPS : StepDiscipline vid S := StepDiscipline.of_storeAgree hAg.symm hPTk
    obtain ⟨hout, _, hS'Eq⟩ := gatedContinuation_right_mover hR hPS hexec
    cases hrhoS : S (.stopKey vid) with
    | some v =>
      have houtPause : out = .paused := by
        rw [hout]; simp [gatedStep, pauseAfterGate, hrhoS]
      subst houtPause
      simp only [StepOutcome.label?]
      have hAfPause : after (gatedStep R vid) ι S =
          (Write.put (.statusKey vid) (.status .paused)).apply S := by
        simp [gatedStep, pauseAfterGate, after, hrhoS]
      rw [hAfPause] at hS'Eq
      have hAg' : StoreAgree vid S' Tk := by
        rw [hS'Eq]
        exact (storeAgree_applyStops vid (MicroAct.stopCount mas) _).trans
          ((storeAgree_putStatus vid .paused S).trans hAg)
      have hSt' : S' (.statusKey vid) = some (.status .paused) := by
        rw [hS'Eq, applyStops_other_key vid _ _ (by intro h; cases h)]
        simp [Write.apply, Write.affects, Write.newVal]
      exact ⟨htr, hidx, Tk, hcp, hAg',
        Or.inl ⟨hTkRun.1, hTkRun.2, .paused, hSt', by decide⟩⟩
    | none =>
      have hι : ι = ιs tr.length := hIn hrhoS
      subst hι
      obtain ⟨hExecEq, hAfterAg⟩ :=
        step_respects_agree (E := E) hR hAg hrhoS hTkRun.2 hPS (ιs tr.length)
      have hRunBoolTk : isStoreRunnableBool vid Tk = true :=
        (isStoreRunnableBool_iff vid Tk).2 hTkRun.1
      cases hCoreTk : exec (stepCore R vid) (ιs tr.length) Tk with
      | mk res asCore =>
        have hCoreS : exec (stepCore R vid) (ιs tr.length) S = (res, asCore) :=
          (stepCore_exec_congr_agree (E := E) hR hAg.symm hPTk (ιs tr.length)).trans hCoreTk
        cases res with
        | error err =>
          have houtFail : out = .failed err := by
            rw [hout]
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoS]
            try dsimp only []
            rw [exec_bind]
            try dsimp only []
            rw [hCoreS]
            dsimp only [finalizeStep]
            simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec]
          subst houtFail
          simp only [StepOutcome.label?]
          have hFailS : (exec (stepCore R vid) (ιs tr.length) S).1 = .error err := by rw [hCoreS]
          have hAgFail := failed_gatedStep_preserves_agree hR hrhoS hFailS
          have hAg' : StoreAgree vid S' Tk := by
            rw [hS'Eq]
            exact (storeAgree_applyStops vid _ _).trans (hAgFail.trans hAg)
          have hSt' : S' (.statusKey vid) = some (.status .error) := by
            rw [hS'Eq, applyStops_other_key vid _ _ (by intro h; cases h)]
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoS]
            try dsimp only []
            rw [after_bind, hFailS]
            dsimp only [finalizeStep]
            simp [Prog.bind_eq, Prog.wr_bind, after_write, Write.apply, Write.affects, Write.newVal]
          exact ⟨htr, hidx, Tk, hcp, hAg',
            Or.inl ⟨hTkRun.1, hTkRun.2, .error, hSt', by decide⟩⟩
        | ok pair =>
          rcases pair with ⟨pi', evs⟩
          let lbl : StepLabel E := ⟨pi', evs⟩
          have houtStep : out = .stepped pi' evs := by
            rw [hout]
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoS]
            try dsimp only []
            rw [exec_bind]
            try dsimp only []
            rw [hCoreS]
            rfl
          subst houtStep
          simp only [StepOutcome.label?]
          have hGatedTk : (exec (gatedStep R vid) (ιs tr.length) Tk).1 = .stepped pi' evs := by
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hTkRun.2]
            try dsimp only []
            rw [exec_bind]
            try dsimp only []
            rw [hCoreTk]
            rfl
          have hLblAt : uninterruptedLabelAt R vid ιs T₀ tr.length = some lbl := by
            simp [uninterruptedLabelAt, hcp, hRunBoolTk, hGatedTk, StepOutcome.label?, lbl]
          let T_raw := after (gatedStep R vid) (ιs tr.length) Tk
          let Tk1 := autoResumePaused vid T_raw
          have hcp1 : commitPointAt R vid ιs T₀ (tr ++ [lbl]).length = some Tk1 := by
            simp [commitPointAt, hcp, hRunBoolTk, hGatedTk, Tk1, T_raw]
          have htr1 : tr ++ [lbl] = uninterruptedPrefix R vid ιs T₀ (tr ++ [lbl]).length := by
            simp only [List.length_append, List.length_singleton, uninterruptedPrefix, hLblAt]
            exact congrArg (· ++ [lbl]) htr
          have hidx1 : ∀ i (hi : i < (tr ++ [lbl]).length),
              uninterruptedLabelAt R vid ιs T₀ i = some (tr ++ [lbl])[i] := by
            intro i hi
            have hi' : i < tr.length + 1 := by simpa using hi
            rcases Nat.lt_or_eq_of_le (Nat.lt_succ_iff.1 hi') with hlt | rfl
            · rw [List.getElem_append_left hlt]
              exact hidx i hlt
            · simp [hLblAt]
          have hAg1 : StoreAgree vid S' Tk1 := by
            rw [hS'Eq]
            exact (storeAgree_applyStops vid _ _).trans
              (hAfterAg.trans (storeAgree_autoResumePaused vid T_raw).symm)
          -- Prove `StatusRel vid S' Tk1`
          obtain ⟨_, _, _, _, _, hStCases⟩ := stepCore_ok_post hR hPTk hCoreTk
          have hS_rawEq : after (gatedStep R vid) (ιs tr.length) S =
              applyAll S (Act.writes asCore) := by
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoS]
            rw [after_bind]
            unfold after at *
            rw [hCoreS]
            rfl
          have hT_rawEq : T_raw = applyAll Tk (Act.writes asCore) := by
            show after (gatedStep R vid) (ιs tr.length) Tk = applyAll Tk (Act.writes asCore)
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, after_read, hTkRun.2]
            rw [after_bind]
            unfold after at *
            rw [hCoreTk]
            rfl
          have hRhoRaw : T_raw (.stopKey vid) = none := by
            have havRho := stepCore_avoids_rho_lambda (E := E) hR hPTk (ιs tr.length)
            have hNotTouchRho : touches (Act.writes asCore) (.stopKey vid) = false := by
              have ht := havRho.not_touches (Or.inl rfl : IsRhoOrLambda vid (.stopKey vid))
              rw [hCoreTk] at ht; exact ht
            rw [hT_rawEq, applyAll_of_not_touches hNotTouchRho]
            exact hTkRun.2
          have hS'Status : S' (.statusKey vid) =
              applyAll S (Act.writes asCore) (.statusKey vid) := by
            rw [hS'Eq, applyStops_other_key vid _ _ (by intro h; cases h), hS_rawEq]
          refine ⟨htr1, hidx1, Tk1, hcp1, hAg1, ?_⟩
          cases ht : touches (Act.writes asCore) (.statusKey vid) with
          | false =>
            obtain ⟨sT, hsT, hsTRun⟩ := hTkRun.1
            have hS'St : S' (.statusKey vid) = some (.status sS) := by
              rw [hS'Status, applyAll_of_not_touches ht, hsS]
            have hTRawSt : T_raw (.statusKey vid) = some (.status sT) := by
              rw [hT_rawEq, applyAll_of_not_touches ht, hsT]
            have hsTNePaused : sT ≠ .paused := by rintro rfl; simp at hsTRun
            have hTk1Eq : Tk1 = T_raw := by
              simp [Tk1, autoResumePaused, hTRawSt, hsTNePaused]
            rw [hTk1Eq]
            exact Or.inl ⟨⟨sT, hTRawSt, hsTRun⟩, hRhoRaw, sS, hS'St, hsNeOk⟩
          | true =>
            rcases hStCases with hUntouched | ⟨s', hs'Raw, hs'NeErr⟩
            · rw [ht] at hUntouched; cases hUntouched
            · have hS'St : S' (.statusKey vid) = some (.status s') := by
                rw [hS'Status, applyAll_eq_of_touches S Tk ht, hs'Raw]
              have hTRawSt : T_raw (.statusKey vid) = some (.status s') := by
                rw [hT_rawEq, hs'Raw]
              cases s' with
              | error => exact absurd rfl hs'NeErr
              | ok =>
                have hTk1Eq : Tk1 = T_raw := by
                  simp [Tk1, autoResumePaused, hTRawSt]
                rw [hTk1Eq]
                exact Or.inr ⟨hTRawSt, hS'St⟩
              | paused =>
                have hTk1Eq : Tk1 = resumeStore vid T_raw := by
                  simp [Tk1, autoResumePaused, hTRawSt]
                rw [hTk1Eq]
                obtain ⟨hStRes, hRhoRes⟩ := resumeStore_status_and_rho vid T_raw
                exact Or.inl ⟨⟨.running, hStRes, rfl⟩, hRhoRes, .paused, hS'St, by decide⟩
              | init =>
                have hTk1Eq : Tk1 = T_raw := by
                  simp [Tk1, autoResumePaused, hTRawSt]
                rw [hTk1Eq]
                exact Or.inl ⟨⟨.init, hTRawSt, rfl⟩, hRhoRaw, .init, hS'St, by decide⟩
              | running =>
                have hTk1Eq : Tk1 = T_raw := by
                  simp [Tk1, autoResumePaused, hTRawSt]
                rw [hTk1Eq]
                exact Or.inl ⟨⟨.running, hTRawSt, rfl⟩, hRhoRaw, .running, hS'St, by decide⟩
              | wait =>
                have hTk1Eq : Tk1 = T_raw := by
                  simp [Tk1, autoResumePaused, hTRawSt]
                rw [hTk1Eq]
                exact Or.inl ⟨⟨.wait, hTRawSt, rfl⟩, hRhoRaw, .wait, hS'St, by decide⟩

/-- **Theorem 3 Finite-Trace Prefix Corollary (`stop_resume_isPrefix`)**:
if the uninterrupted run halts (either at `ok` or at a failure) after emitting
at most `N` labels (`uninterruptedLabelAt R vid ιs T₀ N = none`), then every
`ResumableRun` from `T₀` emits `tr.length ≤ N` labels and `tr` is a prefix of
`tr₀ = uninterruptedPrefix R vid ιs T₀ N`. -/
theorem stop_resume_isPrefix {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ : Store Key (Val D)}
    (hInit : ValidCommitPoint vid T₀)
    (hP : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    {N : Nat} (hHalts : uninterruptedLabelAt R vid ιs T₀ N = none)
    {tr : List (StepLabel E)} {S : Store Key (Val D)}
    (hRun : ResumableRun R vid ιs T₀ tr S) :
    tr.length ≤ N ∧ tr <+: uninterruptedPrefix R vid ιs T₀ N := by
  obtain ⟨htr, hidx, _⟩ := stop_resume_resumption hR hInit hP hRun
  have hle : tr.length ≤ N := by
    apply Classical.byContradiction
    intro hgt
    have hlt : N < tr.length := by omega
    have hSome := hidx N hlt
    rw [hHalts] at hSome
    cases hSome
  refine ⟨hle, ?_⟩
  rw [htr]
  exact uninterruptedPrefix_mono R vid ιs T₀ hle

/-- **Theorem 3 Completed-Run Corollary (`stop_resume_completed`)**:
if the uninterrupted run reaches terminal `ok` at commit point `T_N` (`N` step
labels), then any `ResumableRun` from `T₀` that completes `N` steps (`tr.length = N`),
even with arbitrary late `stop`s after the final step:
1. produces the exact full uninterrupted trace `uninterruptedPrefix R vid ιs T₀ N`,
2. ends in a store `StoreAgree`-equivalent to `T_N` (`S ~ T_N`), and
3. preserves `S (.statusKey vid) = some (.status .ok)` (no stop overwrites `ok`). -/
theorem stop_resume_completed {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ T_N : Store Key (Val D)}
    (hInit : ValidCommitPoint vid T₀)
    (hP : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    {N : Nat}
    (hTN : commitPointAt R vid ιs T₀ N = some T_N)
    (hOkTN : T_N (.statusKey vid) = some (.status .ok))
    {tr : List (StepLabel E)} {S : Store Key (Val D)}
    (hRun : ResumableRun R vid ιs T₀ tr S)
    (hLen : tr.length = N) :
    tr = uninterruptedPrefix R vid ιs T₀ N ∧
    StoreAgree vid S T_N ∧
    S (.statusKey vid) = some (.status .ok) := by
  obtain ⟨htr, _, Tk, hcp, hAg, hRel⟩ := stop_resume_resumption hR hInit hP hRun
  rw [hLen] at htr hcp
  rw [hTN] at hcp
  cases hcp
  refine ⟨htr, hAg, ?_⟩
  rcases hRel with ⟨⟨s, hs, hrun⟩, _⟩ | ⟨_, hOkS⟩
  · rw [hOkTN] at hs; cases hs; simp at hrun
  · exact hOkS

end Resumption

end Pathlang


