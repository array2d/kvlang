import Pathlang.Agreement

/-!
# Unread-Paths Lemma, Theorem 5 (Stop-Edit-Resume), and Live-Edit Simulation (T4 + T4c)

1. Single-step and multi-step Unread-Paths Lemma (`lem:unread`:
   `gatedStep_unread_strict`, `gatedStep_unread`, `uninterrupted_unread`).
2. Lease-disciplined v2 edit protocol (`ValidEdit`, `EditorSession`) and
   **Theorem 5(a)** (`stop_edit_resume_det`) and **Theorem 5(b)**
   (`stop_edit_resume_unread`, plus sharp prefix-bounded `stop_edit_resume_unread_prefix`).
3. **T4c**: Compositional live-edit splicing theorem (`live_edit_spliced_program_equiv`,
   matching `edit-tool` Case C1 without replay) and coordinate-relocation
   step simulation (`return_relocated_step_sim`, `copy_relocated_step_sim`,
   and concrete counterexample `unmigrated_returnpc_diverges` matching Case C2).
-/

namespace Pathlang

/-! ## Agreement Outside an Unexposed Key Set (`AgreeOutside`) and `lem:unread` -/

section UnreadPaths

variable {D I E : Type}

/-- Every execution of `stepCore` reads `kappa = /vthread/{vid}/..pc` as its
first action, so `kappa` is always exposed in `(exec (stepCore R vid) ι T).2`. -/
theorem stepCore_exposes_pcKey (R : Runtime D I E) (vid : Vid)
    (ι : Nat → I) (T : Store Key (Val D)) :
    Act.exposed (exec (stepCore R vid) ι T).2 (.pcKey vid) = true := by
  unfold stepCore
  simp [Prog.bind_eq, Prog.rd_bind, exec_read]

/-- Store agreement outside a difference predicate `Diff` and `IgnoredByAgree`:
`T` and `U` hold the same counter at `kappa` and agree at every key outside
`Diff` and `IgnoredByAgree vid (liveDepthOf vid T)`. -/
def AgreeOutside (vid : Vid) (Diff : Key → Prop)
    (T U : Store Key (Val D)) : Prop :=
  T (.pcKey vid) = U (.pcKey vid) ∧
  ∀ k, ¬ Diff k → ¬ IgnoredByAgree vid (liveDepthOf vid T) k → T k = U k

theorem AgreeOutside.of_storeAgree {vid : Vid} {Diff : Key → Prop}
    {T U : Store Key (Val D)} (h : StoreAgree vid T U) :
    AgreeOutside vid Diff T U :=
  ⟨h.1, fun k _ hk => h.2 k hk⟩

theorem AgreeOutside.to_storeAgree_empty {vid : Vid} {T U : Store Key (Val D)}
    (h : AgreeOutside vid (fun _ => False) T U) :
    StoreAgree vid T U :=
  ⟨h.1, fun k hk => h.2 k (fun f => f) hk⟩

theorem AgreeOutside.mono {vid : Vid} {Diff₁ Diff₂ : Key → Prop}
    {T U : Store Key (Val D)} (hsub : ∀ k, Diff₁ k → Diff₂ k)
    (h : AgreeOutside vid Diff₁ T U) :
    AgreeOutside vid Diff₂ T U :=
  ⟨h.1, fun k hk₂ hkIg => h.2 k (fun hk₁ => hk₂ (hsub k hk₁)) hkIg⟩

theorem AgreeOutside.trans_storeAgree_right {vid : Vid} {Diff : Key → Prop}
    {T U W : Store Key (Val D)}
    (h₁ : AgreeOutside vid Diff T U) (h₂ : StoreAgree vid U W) :
    AgreeOutside vid Diff T W := by
  refine ⟨h₁.1.trans h₂.1, ?_⟩
  intro k hnd hkIg
  have hkIgU : ¬ IgnoredByAgree vid (liveDepthOf vid U) k := by
    rw [← liveDepthOf_eq_of_pc_eq h₁.1]
    exact hkIg
  exact (h₁.2 k hnd hkIg).trans (h₂.2 k hkIgU)

theorem AgreeOutside.trans_storeAgree_left {vid : Vid} {Diff : Key → Prop}
    {T₀ T U : Store Key (Val D)}
    (h₁ : StoreAgree vid T₀ T) (h₂ : AgreeOutside vid Diff T₀ U) :
    AgreeOutside vid Diff T U := by
  refine ⟨h₁.1.symm.trans h₂.1, ?_⟩
  intro k hnd hkIg
  have hkIg0 : ¬ IgnoredByAgree vid (liveDepthOf vid T₀) k := by
    rw [liveDepthOf_eq_of_pc_eq h₁.1]
    exact hkIg
  exact (h₁.2 k hkIg0).symm.trans (h₂.2 k hnd hkIg0)

private theorem isDeadFrameKey_antitone' {vid : Vid} {d₁ d₂ : Nat} {k : Key}
    (hle : d₁ ≤ d₂) (h : isDeadFrameKey vid d₂ k = true) :
    isDeadFrameKey vid d₁ k = true := by
  cases k <;> simp only [isDeadFrameKey, Bool.false_eq_true, decide_eq_true_eq] at h ⊢ <;>
    (try contradiction) <;>
    rcases h with ⟨rfl, h⟩ <;> exact ⟨rfl, by omega⟩

private theorem inFrame_of_dead_not_dead_succ' {vid : Vid} {d : Nat} {k : Key}
    (hd : isDeadFrameKey vid d k = true)
    (hnd : ¬ (isDeadFrameKey vid (d + 1) k = true)) :
    inFrame vid (d + 1) k = true := by
  have hArith : ∀ e, (e = 0 ∨ d < e) → ¬ (e = 0 ∨ d + 1 < e) → e = d + 1 := by omega
  cases k <;>
    simp only [isDeadFrameKey, inFrame, Bool.false_eq_true, decide_eq_true_eq] at hd hnd ⊢ <;>
    (try contradiction) <;>
    rcases hd with ⟨rfl, hd⟩ <;>
    exact ⟨rfl, hArith _ hd (fun h => hnd ⟨rfl, h⟩)⟩

private theorem exec_finalizeStep_indep' (vid : Vid) (res : Except ErrMsg (PC × List E))
    (ι₁ ι₂ : Nat → I) (S₁ S₂ : Store Key (Val D)) :
    exec (finalizeStep (E := E) vid res) ι₁ S₁ = exec (finalizeStep vid res) ι₂ S₂ := by
  cases res with
  | ok p => rcases p with ⟨_, _⟩; rfl
  | error _ => rfl

/-- **Single-Step Unread-Paths Lemma (`lem:unread`, agreement form)**:
if `T` and `U` both pass the gate (`rho = none`), `T` satisfies `(P)`
(`StepDiscipline vid T`), `T` and `U` agree outside `Diff ∪ IgnoredByAgree`,
and no key in `Diff` is exposed in `(exec (stepCore R vid) ι T).2`, then:
1. `stepCore` and `gatedStep` perform the **exact same actions and outcome**
   from `U` as from `T`;
2. on a failed step, post-step stores agree outside `Diff ∪ IgnoredByAgree`;
3. on a successful step, post-step stores agree outside
   `(Diff \ writes) ∪ IgnoredByAgree`. -/
theorem gatedStep_unread {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {Diff : Key → Prop} {T U : Store Key (Val D)}
    (hOut : ∀ k, ¬ Diff k → ¬ IgnoredByAgree vid (liveDepthOf vid T) k → T k = U k)
    (hrhoT : T (.stopKey vid) = none)
    (hrhoU : U (.stopKey vid) = none)
    (hP : StepDiscipline vid T)
    (ι : Nat → I)
    (hx : ∀ k, Diff k → Act.exposed (exec (stepCore R vid) ι T).2 k = false) :
    exec (stepCore R vid) ι U = exec (stepCore R vid) ι T ∧
    exec (gatedStep R vid) ι U = exec (gatedStep R vid) ι T ∧
    (∀ err, (exec (stepCore R vid) ι T).1 = .error err →
      AgreeOutside vid Diff (after (gatedStep R vid) ι T) (after (gatedStep R vid) ι U)) ∧
    (∀ pi' evs, (exec (stepCore R vid) ι T).1 = .ok (pi', evs) →
      AgreeOutside vid
        (fun k => Diff k ∧ touches (Act.writes (exec (stepCore R vid) ι T).2) k = false)
        (after (gatedStep R vid) ι T)
        (after (gatedStep R vid) ι U)) := by
  have hpcNotDiff : ¬ Diff (.pcKey vid) := by
    intro hDiffPc
    have hexp := stepCore_exposes_pcKey R vid ι T
    rw [hx (.pcKey vid) hDiffPc] at hexp
    cases hexp
  have hpcEq : T (.pcKey vid) = U (.pcKey vid) :=
    hOut (.pcKey vid) hpcNotDiff (pcKey_not_ignored vid (liveDepthOf vid T))
  have hAgreeOut : AgreeOutside vid Diff T U := ⟨hpcEq, hOut⟩
  have hav := stepCore_reads_not_ignored (E := E) hR hP ι
  have hCoreEq : exec (stepCore R vid) ι U = exec (stepCore R vid) ι T := by
    apply exec_congr (stepCore R vid) ι T U
      (fun k => Diff k ∨ IgnoredByAgree vid (liveDepthOf vid T) k)
    · intro k hk
      exact (hOut k (fun hd => hk (Or.inl hd)) (fun hi => hk (Or.inr hi))).symm
    · intro k hexp hk
      rcases hk with hd | hi
      · rw [hx k hd] at hexp; cases hexp
      · exact hav.not_exposed hexp hi
  have hExecEq : exec (gatedStep R vid) ι U = exec (gatedStep R vid) ι T := by
    unfold gatedStep
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoT, hrhoU]
    try dsimp only []
    rw [exec_bind, exec_bind]
    dsimp only []
    rw [hCoreEq, exec_finalizeStep_indep' vid (exec (stepCore R vid) ι T).1 _ _ _ _]
  refine ⟨hCoreEq, hExecEq, ?_, ?_⟩
  · intro err hFailT
    have hFailU : (exec (stepCore R vid) ι U).1 = .error err := by
      rw [hCoreEq]; exact hFailT
    have hAT := failed_gatedStep_preserves_agree hR hrhoT hFailT
    have hAU := failed_gatedStep_preserves_agree hR hrhoU hFailU
    exact (hAgreeOut.trans_storeAgree_left hAT.symm).trans_storeAgree_right hAU.symm
  · intro pi' evs hOkT
    cases hCore : exec (stepCore R vid) ι T with
    | mk res asCore =>
      rw [hCore] at hOkT
      dsimp only [] at hOkT
      subst hOkT
      have hCoreU : exec (stepCore R vid) ι U = (.ok (pi', evs), asCore) :=
        hCoreEq.trans hCore
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
          exact hpcEq
      refine ⟨hpcEqAfter, ?_⟩
      intro k hNotRem hkIg
      have hdAfter : liveDepthOf vid (applyAll T ws) = pi'.depth :=
        liveDepthOf_pc hpcAfterT
      rw [hdAfter] at hkIg
      cases ht : touches ws k with
      | true => exact applyAll_eq_of_touches T U ht
      | false =>
        have hNotDiff : ¬ Diff k := fun hd => hNotRem ⟨hd, ht⟩
        rw [applyAll_of_not_touches ht, applyAll_of_not_touches ht]
        apply hOut k hNotDiff
        rw [liveDepthOf_pc hpcT]
        rintro (rfl | rfl | rfl | rfl | hdead)
        · exact hkIg (Or.inl rfl)
        · exact hkIg (Or.inr (Or.inl rfl))
        · exact hkIg (Or.inr (Or.inr (Or.inl rfl)))
        · exact hkIg (Or.inr (Or.inr (Or.inr (Or.inl rfl))))
        · rcases hDepth with hle | ⟨hd1, hdelMem⟩
          · exact hkIg (Or.inr (Or.inr (Or.inr (Or.inr (isDeadFrameKey_antitone' hle hdead)))))
          · have hnotDead' : ¬ (isDeadFrameKey vid (pi.depth + 1) k = true) := by
              rw [← hd1]
              exact fun hd' => hkIg (Or.inr (Or.inr (Or.inr (Or.inr hd'))))
            have hinChild := inFrame_of_dead_not_dead_succ' hdead hnotDead'
            have htTrue := touches_of_mem hdelMem (by simpa [Write.affects] using hinChild)
            rw [htTrue] at ht
            cases ht

/-- **Single-Step Strict Unread-Paths Lemma (`lem:unread`, Appendix A.5 form)**:
if `T` and `U` agree outside `Diff ∪ {sigma, rho, lambda}`, both have `rho = none`,
`T` satisfies `(P)`, and no key of `Diff` is exposed in `(exec (stepCore R vid) ι T).2`,
then `gatedStep` performs the exact same actions and outcome from `U` as from `T`,
and on a successful step the resulting stores agree outside
`(Diff \ writes) ∪ {sigma, rho, lambda}`. -/
theorem gatedStep_unread_strict {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {Diff : Key → Prop} {T U : Store Key (Val D)}
    (hOut : ∀ k, ¬ Diff k → k ≠ .statusKey vid → k ≠ .stopKey vid → k ≠ .driverKey vid →
      T k = U k)
    (hrhoT : T (.stopKey vid) = none)
    (hrhoU : U (.stopKey vid) = none)
    (hP : StepDiscipline vid T)
    (ι : Nat → I)
    (hx : ∀ k, Diff k → Act.exposed (exec (stepCore R vid) ι T).2 k = false) :
    exec (gatedStep R vid) ι U = exec (gatedStep R vid) ι T ∧
    (∀ pi' evs, (exec (stepCore R vid) ι T).1 = .ok (pi', evs) →
      ∀ k,
        ¬ (Diff k ∧ touches (Act.writes (exec (stepCore R vid) ι T).2) k = false) →
        k ≠ .statusKey vid → k ≠ .stopKey vid → k ≠ .driverKey vid →
        after (gatedStep R vid) ι T k = after (gatedStep R vid) ι U k) := by
  have hOutAg : ∀ k, ¬ Diff k → ¬ IgnoredByAgree vid (liveDepthOf vid T) k → T k = U k := by
    intro k hnd hkIg
    exact hOut k hnd
      (fun h => hkIg (Or.inl h))
      (fun h => hkIg (Or.inr (Or.inr (Or.inl h))))
      (fun h => hkIg (Or.inr (Or.inr (Or.inr (Or.inl h)))))
  obtain ⟨hCoreEq, hExecEq, _, _⟩ := gatedStep_unread hR hOutAg hrhoT hrhoU hP ι hx
  refine ⟨hExecEq, ?_⟩
  intro pi' evs hOkT k hNotRem hkSt hkRho hkDrv
  cases hCore : exec (stepCore R vid) ι T with
  | mk res asCore =>
    rw [hCore] at hOkT hNotRem
    dsimp only [] at hOkT hNotRem
    subst hOkT
    have hCoreU : exec (stepCore R vid) ι U = (.ok (pi', evs), asCore) :=
      hCoreEq.trans hCore
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
    cases ht : touches ws k with
    | true => exact applyAll_eq_of_touches T U ht
    | false =>
      rw [applyAll_of_not_touches ht, applyAll_of_not_touches ht]
      exact hOut k (fun hd => hNotRem ⟨hd, ht⟩) hkSt hkRho hkDrv

/-! ### Multi-Step Uninterrupted Exposure and `uninterrupted_unread` -/

/-- Whether a runnable commit point's step succeeds and writes `k`. -/
def stepWriteTouches (R : Runtime D I E) (vid : Vid) (ι : Nat → I)
    (T : Store Key (Val D)) (k : Key) : Bool :=
  if isStoreRunnableBool vid T then
    match (exec (stepCore R vid) ι T).1 with
    | .ok _ => touches (Act.writes (exec (stepCore R vid) ι T).2) k
    | .error _ => false
  else
    false

/-- Whether key `k` is written by some completed step `< n` of the uninterrupted
run from `T₀`. -/
def writtenBefore (R : Runtime D I E) (vid : Vid) (ιs : Nat → Nat → I)
    (T₀ : Store Key (Val D)) : Nat → Key → Bool
  | 0, _ => false
  | n + 1, k =>
      writtenBefore R vid ιs T₀ n k ||
      match commitPointAt R vid ιs T₀ n with
      | none => false
      | some Tn => stepWriteTouches R vid (ιs n) Tn k

@[simp] theorem writtenBefore_zero (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) (k : Key) :
    writtenBefore R vid ιs T₀ 0 k = false := rfl

theorem writtenBefore_mono (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) (T₀ : Store Key (Val D)) {i j : Nat} {k : Key}
    (hle : i ≤ j) (h : writtenBefore R vid ιs T₀ i k = true) :
    writtenBefore R vid ιs T₀ j k = true := by
  induction j with
  | zero =>
    have hi0 : i = 0 := by omega
    subst hi0; exact h
  | succ j ih =>
    rcases Nat.eq_or_lt_of_le hle with rfl | hlt
    · exact h
    · simp [writtenBefore, ih (by omega)]

/-- Key `k` is exposed at step index `n` of the uninterrupted run from `T₀` iff
no earlier completed step `< n` wrote `k`, commit point `Tn` is reached and
runnable, and step `n` from `Tn` reads `k` before writing `k`. -/
def stepExposedAt (R : Runtime D I E) (vid : Vid) (ιs : Nat → Nat → I)
    (T₀ : Store Key (Val D)) (n : Nat) (k : Key) : Prop :=
  writtenBefore R vid ιs T₀ n k = false ∧
  ∃ Tn, commitPointAt R vid ιs T₀ n = some Tn ∧
    StoreRunnable vid Tn ∧
    Act.exposed (exec (stepCore R vid) (ιs n) Tn).2 k = true

theorem stepExposedAt_zero_pcKey (R : Runtime D I E) (vid : Vid)
    (ιs : Nat → Nat → I) {T₀ : Store Key (Val D)}
    (hRun : StoreRunnable vid (initCommit vid T₀)) :
    stepExposedAt R vid ιs T₀ 0 (.pcKey vid) :=
  ⟨rfl, initCommit vid T₀, rfl, hRun, stepCore_exposes_pcKey R vid (ιs 0) _⟩

/-- **Multi-Step Unread-Paths Lemma (`lem:unread`, `uninterrupted_unread`)**:
for any `N ≥ 1`, if `initCommit vid T` and `initCommit vid U` are both runnable
with `rho = none`, agree outside `Diff ∪ IgnoredByAgree`, `T`'s commit sequence
satisfies `(P)`, and no key in `Diff` is exposed at any step `< N` from `T`, then:
1. for every `j < N`, `uninterruptedLabelAt R vid ιs U j = uninterruptedLabelAt R vid ιs T j`;
2. for every `j ≤ N`, `uninterruptedPrefix R vid ιs U j = uninterruptedPrefix R vid ιs T j`;
3. for every `j ≤ N`, `commitPointAt R vid ιs T j = none ↔ commitPointAt R vid ιs U j = none`,
   and whenever `commitPointAt R vid ιs T j = some Tj`, there exists `Uj` with
   `commitPointAt R vid ιs U j = some Uj`,
   `AgreeOutside vid (fun k => Diff k ∧ writtenBefore R vid ιs T j k = false) Tj Uj`,
   `(StoreRunnable vid Tj ↔ StoreRunnable vid Uj)`, `Tj (.stopKey vid) = none`,
   and `Uj (.stopKey vid) = none`. -/
theorem uninterrupted_unread {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {Diff : Key → Prop} {ιs : Nat → Nat → I}
    {T U : Store Key (Val D)} {N : Nat} (hN : 1 ≤ N)
    (hRunT : StoreRunnable vid (initCommit vid T))
    (hRunU : StoreRunnable vid (initCommit vid U))
    (hrhoT : initCommit vid T (.stopKey vid) = none)
    (hrhoU : initCommit vid U (.stopKey vid) = none)
    (hOut : ∀ k, ¬ Diff k → ¬ IgnoredByAgree vid (liveDepthOf vid (initCommit vid T)) k →
      initCommit vid T k = initCommit vid U k)
    (hP : ∀ j Tj, commitPointAt R vid ιs T j = some Tj →
      StoreRunnable vid Tj → StepDiscipline vid Tj)
    (hx : ∀ j, j < N → ∀ k, Diff k → ¬ stepExposedAt R vid ιs T j k) :
    (∀ j, j < N → uninterruptedLabelAt R vid ιs U j = uninterruptedLabelAt R vid ιs T j) ∧
    (∀ j, j ≤ N → uninterruptedPrefix R vid ιs U j = uninterruptedPrefix R vid ιs T j) ∧
    (∀ j, j ≤ N →
      (commitPointAt R vid ιs T j = none ↔ commitPointAt R vid ιs U j = none) ∧
      (∀ Tj, commitPointAt R vid ιs T j = some Tj →
        ∃ Uj, commitPointAt R vid ιs U j = some Uj ∧
          AgreeOutside vid (fun k => Diff k ∧ writtenBefore R vid ιs T j k = false) Tj Uj ∧
          (StoreRunnable vid Tj ↔ StoreRunnable vid Uj) ∧
          Tj (.stopKey vid) = none ∧
          Uj (.stopKey vid) = none)) := by
  have hpcNotDiff : ¬ Diff (.pcKey vid) := by
    intro hDiffPc
    exact hx 0 (by omega) (.pcKey vid) hDiffPc (stepExposedAt_zero_pcKey R vid ιs hRunT)
  have hpcEq0 : initCommit vid T (.pcKey vid) = initCommit vid U (.pcKey vid) :=
    hOut (.pcKey vid) hpcNotDiff (pcKey_not_ignored vid _)
  have hStepInv : ∀ j, j ≤ N →
      (commitPointAt R vid ιs T j = none ↔ commitPointAt R vid ιs U j = none) ∧
      (∀ Tj, commitPointAt R vid ιs T j = some Tj →
        ∃ Uj, commitPointAt R vid ιs U j = some Uj ∧
          AgreeOutside vid (fun k => Diff k ∧ writtenBefore R vid ιs T j k = false) Tj Uj ∧
          (StoreRunnable vid Tj ↔ StoreRunnable vid Uj) ∧
          Tj (.stopKey vid) = none ∧
          Uj (.stopKey vid) = none) := by
    intro j hjLe
    induction j with
    | zero =>
      refine ⟨by simp [commitPointAt], ?_⟩
      intro Tj hcpT
      simp only [commitPointAt, Option.some.injEq] at hcpT
      subst hcpT
      refine ⟨initCommit vid U, rfl, ⟨hpcEq0, ?_⟩, ⟨fun _ => hRunU, fun _ => hRunT⟩, hrhoT, hrhoU⟩
      intro k hkNot hkIg
      exact hOut k (fun hd => hkNot ⟨hd, rfl⟩) hkIg
    | succ j ih =>
      obtain ⟨hNoneIff, hSomeInv⟩ := ih (by omega)
      cases hcpT : commitPointAt R vid ιs T j with
      | none =>
        have hcpU : commitPointAt R vid ιs U j = none := hNoneIff.1 hcpT
        simp [commitPointAt, hcpT, hcpU]
      | some Tj =>
        obtain ⟨Uj, hcpU, hAgj, hRunIff, hrhoTj, hrhoUj⟩ := hSomeInv Tj hcpT
        cases hRunBoolT : isStoreRunnableBool vid Tj with
        | false =>
          have hRunBoolU : isStoreRunnableBool vid Uj = false := by
            cases hb : isStoreRunnableBool vid Uj with
            | false => rfl
            | true =>
              have hrU := (isStoreRunnableBool_iff vid Uj).1 hb
              have hrT := (isStoreRunnableBool_iff vid Tj).2 (hRunIff.2 hrU)
              rw [hRunBoolT] at hrT; cases hrT
          simp [commitPointAt, hcpT, hcpU, hRunBoolT, hRunBoolU]
        | true =>
          have hRunTj : StoreRunnable vid Tj := (isStoreRunnableBool_iff vid Tj).1 hRunBoolT
          have hRunUj : StoreRunnable vid Uj := hRunIff.1 hRunTj
          have hRunBoolU : isStoreRunnableBool vid Uj = true :=
            (isStoreRunnableBool_iff vid Uj).2 hRunUj
          have hPTj : StepDiscipline vid Tj := hP j Tj hcpT hRunTj
          have hxj : ∀ k, (Diff k ∧ writtenBefore R vid ιs T j k = false) →
              Act.exposed (exec (stepCore R vid) (ιs j) Tj).2 k = false := by
            rintro k ⟨hd, hwb⟩
            cases hexp : Act.exposed (exec (stepCore R vid) (ιs j) Tj).2 k with
            | false => rfl
            | true =>
              exact absurd ⟨hwb, Tj, hcpT, hRunTj, hexp⟩ (hx j (by omega) k hd)
          obtain ⟨hCoreEq, hExecEq, _, hOkPost⟩ :=
            gatedStep_unread (E := E) hR hAgj.2 hrhoTj hrhoUj hPTj (ιs j) hxj
          cases hCoreT : exec (stepCore R vid) (ιs j) Tj with
          | mk res asCore =>
            have hCoreU : exec (stepCore R vid) (ιs j) Uj = (res, asCore) :=
              hCoreEq.trans hCoreT
            cases res with
            | error err =>
              have hGatedT : (exec (gatedStep R vid) (ιs j) Tj).1 = .failed err := by
                unfold gatedStep
                simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoTj]
                try dsimp only []
                rw [exec_bind]; try dsimp only []
                rw [hCoreT]; dsimp only [finalizeStep]
                simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec]
              have hGatedU : (exec (gatedStep R vid) (ιs j) Uj).1 = .failed err := by
                rw [hExecEq]; exact hGatedT
              simp [commitPointAt, hcpT, hcpU, hRunBoolT, hRunBoolU, hGatedT, hGatedU]
            | ok pair =>
              rcases pair with ⟨pi', evs⟩
              have hGatedT : (exec (gatedStep R vid) (ιs j) Tj).1 = .stepped pi' evs := by
                unfold gatedStep
                simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoTj]
                try dsimp only []
                rw [exec_bind]; try dsimp only []
                rw [hCoreT]; rfl
              have hGatedU : (exec (gatedStep R vid) (ιs j) Uj).1 = .stepped pi' evs := by
                rw [hExecEq]; exact hGatedT
              let T_raw := after (gatedStep R vid) (ιs j) Tj
              let U_raw := after (gatedStep R vid) (ιs j) Uj
              let Tj1 := autoResumePaused vid T_raw
              let Uj1 := autoResumePaused vid U_raw
              have hcpT1 : commitPointAt R vid ιs T (j + 1) = some Tj1 := by
                simp [commitPointAt, hcpT, hRunBoolT, hGatedT, Tj1, T_raw]
              have hcpU1 : commitPointAt R vid ιs U (j + 1) = some Uj1 := by
                simp [commitPointAt, hcpU, hRunBoolU, hGatedU, Uj1, U_raw]
              have hOkT1 : (exec (stepCore R vid) (ιs j) Tj).1 = .ok (pi', evs) := by
                rw [hCoreT]
              have hAgRaw := hOkPost pi' evs hOkT1
              have hWbEq : ∀ k,
                  ((Diff k ∧ writtenBefore R vid ιs T j k = false) ∧
                    touches (Act.writes (exec (stepCore R vid) (ιs j) Tj).2) k = false) ↔
                  (Diff k ∧ writtenBefore R vid ιs T (j + 1) k = false) := by
                intro k
                simp [writtenBefore, hcpT, stepWriteTouches, hRunBoolT, hCoreT, and_assoc]
              have hAgRaw' : AgreeOutside vid
                  (fun k => Diff k ∧ writtenBefore R vid ιs T (j + 1) k = false) T_raw U_raw := by
                refine ⟨hAgRaw.1, ?_⟩
                intro k hNot hkIg
                exact hAgRaw.2 k (fun h => hNot ((hWbEq k).1 h)) hkIg
              have hAg1 : AgreeOutside vid
                  (fun k => Diff k ∧ writtenBefore R vid ιs T (j + 1) k = false) Tj1 Uj1 :=
                (hAgRaw'.trans_storeAgree_left
                  (storeAgree_autoResumePaused vid T_raw).symm).trans_storeAgree_right
                  (storeAgree_autoResumePaused vid U_raw).symm
              obtain ⟨_, _, _, _, _, hStCases⟩ := stepCore_ok_post hR hPTj hCoreT
              let ws := Act.writes asCore
              have hTRawEq : T_raw = applyAll Tj ws := by
                show after (gatedStep R vid) (ιs j) Tj = applyAll Tj ws
                unfold gatedStep
                simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoTj]
                rw [after_bind]; unfold after at *; rw [hCoreT]; rfl
              have hURawEq : U_raw = applyAll Uj ws := by
                show after (gatedStep R vid) (ιs j) Uj = applyAll Uj ws
                unfold gatedStep
                simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoUj]
                rw [after_bind]; unfold after at *; rw [hCoreU]; rfl
              have havRho := stepCore_avoids_rho_lambda (E := E) hR hPTj (ιs j)
              have hNotTouchRho : touches ws (.stopKey vid) = false := by
                have ht := havRho.not_touches (Or.inl rfl : IsRhoOrLambda vid (.stopKey vid))
                rw [hCoreT] at ht; exact ht
              have hRhoTRaw : T_raw (.stopKey vid) = none := by
                rw [hTRawEq, applyAll_of_not_touches hNotTouchRho, hrhoTj]
              have hRhoURaw : U_raw (.stopKey vid) = none := by
                rw [hURawEq, applyAll_of_not_touches hNotTouchRho, hrhoUj]
              have hRhoAuto : ∀ (S : Store Key (Val D)), S (.stopKey vid) = none →
                  autoResumePaused vid S (.stopKey vid) = none := by
                intro S hS
                unfold autoResumePaused
                cases S (.statusKey vid) with
                | none => exact hS
                | some v =>
                  cases v <;> try exact hS
                  rename_i s
                  cases s <;> try exact hS
                  simp [resumeStore, Write.apply, Write.affects, Write.newVal]
              have hRhoTj1 : Tj1 (.stopKey vid) = none := hRhoAuto T_raw hRhoTRaw
              have hRhoUj1 : Uj1 (.stopKey vid) = none := hRhoAuto U_raw hRhoURaw
              have hRunIff1 : StoreRunnable vid Tj1 ↔ StoreRunnable vid Uj1 := by
                cases htSt : touches ws (.statusKey vid) with
                | false =>
                  obtain ⟨sT, hsT, hsTRun⟩ := hRunTj
                  obtain ⟨sU, hsU, hsURun⟩ := hRunUj
                  have hTRawSt : T_raw (.statusKey vid) = some (.status sT) := by
                    rw [hTRawEq, applyAll_of_not_touches htSt, hsT]
                  have hURawSt : U_raw (.statusKey vid) = some (.status sU) := by
                    rw [hURawEq, applyAll_of_not_touches htSt, hsU]
                  have hsTNeP : sT ≠ .paused := by rintro rfl; simp at hsTRun
                  have hsUNeP : sU ≠ .paused := by rintro rfl; simp at hsURun
                  have hTj1Eq : Tj1 = T_raw := by simp [Tj1, autoResumePaused, hTRawSt, hsTNeP]
                  have hUj1Eq : Uj1 = U_raw := by simp [Uj1, autoResumePaused, hURawSt, hsUNeP]
                  rw [hTj1Eq, hUj1Eq]
                  exact ⟨fun _ => ⟨sU, hURawSt, hsURun⟩, fun _ => ⟨sT, hTRawSt, hsTRun⟩⟩
                | true =>
                  rcases hStCases with hUntouched | ⟨s', hs'T, _⟩
                  · rw [htSt] at hUntouched; cases hUntouched
                  · have hTRawSt : T_raw (.statusKey vid) = some (.status s') := by
                      rw [hTRawEq, hs'T]
                    have hURawSt : U_raw (.statusKey vid) = some (.status s') := by
                      rw [hURawEq, applyAll_eq_of_touches Uj Tj htSt, hs'T]
                    have hAutoStEq : Tj1 (.statusKey vid) = Uj1 (.statusKey vid) := by
                      cases s' <;>
                        simp [Tj1, Uj1, autoResumePaused, hTRawSt, hURawSt,
                          resumeStore, Write.apply, Write.affects, Write.newVal]
                    unfold StoreRunnable
                    rw [hAutoStEq]
              rw [hcpT1, hcpU1]
              refine ⟨by simp, ?_⟩
              intro Tk1 hk1
              cases hk1
              exact ⟨Uj1, rfl, hAg1, hRunIff1, hRhoTj1, hRhoUj1⟩
  have hLblEq : ∀ j, j < N →
      uninterruptedLabelAt R vid ιs U j = uninterruptedLabelAt R vid ιs T j := by
    intro j hjLt
    obtain ⟨hNoneIff, hSomeInv⟩ := hStepInv j (by omega)
    cases hcpT : commitPointAt R vid ιs T j with
    | none =>
      have hcpU := hNoneIff.1 hcpT
      simp [uninterruptedLabelAt, hcpT, hcpU]
    | some Tj =>
      obtain ⟨Uj, hcpU, hAgj, hRunIff, hrhoTj, hrhoUj⟩ := hSomeInv Tj hcpT
      cases hRunBoolT : isStoreRunnableBool vid Tj with
      | false =>
        have hRunBoolU : isStoreRunnableBool vid Uj = false := by
          cases hb : isStoreRunnableBool vid Uj with
          | false => rfl
          | true =>
            have hrU := (isStoreRunnableBool_iff vid Uj).1 hb
            have hrT := (isStoreRunnableBool_iff vid Tj).2 (hRunIff.2 hrU)
            rw [hRunBoolT] at hrT; cases hrT
        simp [uninterruptedLabelAt, hcpT, hcpU, hRunBoolT, hRunBoolU]
      | true =>
        have hRunTj := (isStoreRunnableBool_iff vid Tj).1 hRunBoolT
        have hRunBoolU := (isStoreRunnableBool_iff vid Uj).2 (hRunIff.1 hRunTj)
        have hPTj := hP j Tj hcpT hRunTj
        have hxj : ∀ k, (Diff k ∧ writtenBefore R vid ιs T j k = false) →
            Act.exposed (exec (stepCore R vid) (ιs j) Tj).2 k = false := by
          rintro k ⟨hd, hwb⟩
          cases hexp : Act.exposed (exec (stepCore R vid) (ιs j) Tj).2 k with
          | false => rfl
          | true =>
            exact absurd ⟨hwb, Tj, hcpT, hRunTj, hexp⟩ (hx j hjLt k hd)
        obtain ⟨_, hExecEq, _, _⟩ :=
          gatedStep_unread (E := E) hR hAgj.2 hrhoTj hrhoUj hPTj (ιs j) hxj
        simp [uninterruptedLabelAt, hcpT, hcpU, hRunBoolT, hRunBoolU, hExecEq]
  have hPrefEq : ∀ j, j ≤ N →
      uninterruptedPrefix R vid ιs U j = uninterruptedPrefix R vid ιs T j := by
    intro j hjLe
    induction j with
    | zero => rfl
    | succ j ih =>
      simp only [uninterruptedPrefix, hLblEq j (by omega), ih (by omega)]
  exact ⟨hLblEq, hPrefEq, hStepInv⟩

end UnreadPaths

/-! ## v2 Lease-Disciplined Edit Protocol and Theorem 5 (`thm:edit`) -/

section StopEditResume

variable {D I E : Type}

/-- A single edit write respects the v2 Editor invariants (line 31):
it does not affect `sigma`, `rho`, `lambda`, or any queue entry of `vid`. -/
def ValidEditWrite (vid : Vid) (w : Write Key (Val D)) : Prop :=
  w.affects (.statusKey vid) = false ∧
  w.affects (.stopKey vid) = false ∧
  w.affects (.driverKey vid) = false ∧
  ∀ op, w.affects (.queueEntry op vid) = false

/-- A valid edit sequence consists of `ValidEditWrite` operations. -/
def ValidEdit (vid : Vid) (edits : List (Write Key (Val D))) : Prop :=
  ∀ w ∈ edits, ValidEditWrite vid w

theorem validEdit_not_touches_status {vid : Vid} {edits : List (Write Key (Val D))}
    (h : ValidEdit vid edits) : touches edits (.statusKey vid) = false := by
  cases ht : touches edits (.statusKey vid) with
  | false => rfl
  | true =>
    obtain ⟨w, hw, haff⟩ := touches_iff.1 ht
    rw [(h w hw).1] at haff; cases haff

theorem validEdit_not_touches_stop {vid : Vid} {edits : List (Write Key (Val D))}
    (h : ValidEdit vid edits) : touches edits (.stopKey vid) = false := by
  cases ht : touches edits (.stopKey vid) with
  | false => rfl
  | true =>
    obtain ⟨w, hw, haff⟩ := touches_iff.1 ht
    rw [(h w hw).2.1] at haff; cases haff

theorem validEdit_not_touches_driver {vid : Vid} {edits : List (Write Key (Val D))}
    (h : ValidEdit vid edits) : touches edits (.driverKey vid) = false := by
  cases ht : touches edits (.driverKey vid) with
  | false => rfl
  | true =>
    obtain ⟨w, hw, haff⟩ := touches_iff.1 ht
    rw [(h w hw).2.2.1] at haff; cases haff

theorem validEdit_not_touches_queue {vid : Vid} {edits : List (Write Key (Val D))}
    (h : ValidEdit vid edits) (op : SymName) :
    touches edits (.queueEntry op vid) = false := by
  cases ht : touches edits (.queueEntry op vid) with
  | false => rfl
  | true =>
    obtain ⟨w, hw, haff⟩ := touches_iff.1 ht
    rw [(h w hw).2.2.2 op] at haff; cases haff

/-- Pointwise congruence of `applyAll` at a single key `k`. -/
theorem applyAll_eval_eq_of_eq {K V : Type} [DecidableEq K]
    {T₁ T₂ : Store K V} (ws : List (Write K V)) {k : K}
    (hk : T₁ k = T₂ k) : applyAll T₁ ws k = applyAll T₂ ws k := by
  induction ws generalizing T₁ T₂ with
  | nil => exact hk
  | cons w ws ih =>
    rw [applyAll_cons, applyAll_cons]
    apply ih
    cases haff : w.affects k with
    | true => rw [Write.apply_of_affects T₁ haff, Write.apply_of_affects T₂ haff]
    | false => rw [Write.apply_of_not_affects T₁ haff, Write.apply_of_not_affects T₂ haff, hk]

/-- **v2 Lease-Disciplined Editor Session (lines 29-33)**:
1. Acquires `lambda = /vthread/{vid}/..driver` via atomic `createIfAbsent`
   (succeeding iff `S(lambda) = none`, preventing any driver or concurrent editor
   from running during the edit by `lease_mutual_exclusion`);
2. Verifies `S(sigma) ∈ {paused, error}` (`s.isResumable = true`);
3. Applies `edits` (`ValidEdit vid edits`);
4. Releases `lambda` (`Write.del (.driverKey vid)`). -/
def EditorSession (vid : Vid) (tok : Token) (edits : List (Write Key (Val D)))
    (S S' : Store Key (Val D)) : Prop :=
  (createIfAbsent (.driverKey vid) (.lease tok) S).1 = true ∧
  (∃ s, S (.statusKey vid) = some (.status s) ∧ s.isResumable = true) ∧
  ValidEdit vid edits ∧
  S' = (Write.del (.driverKey vid)).apply
    (applyAll (createIfAbsent (.driverKey vid) (.lease tok) S).2 edits)

/-- Because `S(lambda) = none`, `edits` never touches `lambda`, and the editor
releases `lambda` via `del(lambda)`, the resulting store `S'` equals
`applyAll S edits` everywhere while guaranteeing `ResumeEnabled vid S'` and
`ValidCommitPoint vid S'`. -/
theorem EditorSession.props {vid : Vid} {tok : Token}
    {edits : List (Write Key (Val D))} {S S' : Store Key (Val D)}
    (hEd : EditorSession vid tok edits S S') :
    S (.driverKey vid) = none ∧
    S' = applyAll S edits ∧
    S' (.driverKey vid) = none ∧
    S' (.statusKey vid) = S (.statusKey vid) ∧
    ResumeEnabled vid S' ∧
    ValidCommitPoint vid S' := by
  obtain ⟨hAcq, ⟨s, hs, hres⟩, hValid, hS'Def⟩ := hEd
  have hFree : S (.driverKey vid) = none := by
    cases hDrv : S (.driverKey vid) with
    | none => rfl
    | some v => rw [createIfAbsent_present _ v hDrv] at hAcq; simp at hAcq
  have hS'Eq : S' = applyAll S edits := by
    rw [hS'Def, createIfAbsent_absent _ hFree]
    funext k
    by_cases hk : k = .driverKey vid
    · subst hk
      rw [Write.apply_of_affects _ (by simp [Write.affects]),
          applyAll_of_not_touches (validEdit_not_touches_driver hValid)]
      exact hFree.symm
    · rw [Write.apply_of_not_affects _ (by simp [Write.affects, hk])]
      apply applyAll_eval_eq_of_eq
      exact Write.apply_of_not_affects S (by simp [Write.affects, hk])
  have hDrv' : S' (.driverKey vid) = none := by
    rw [hS'Eq, applyAll_of_not_touches (validEdit_not_touches_driver hValid), hFree]
  have hSt' : S' (.statusKey vid) = S (.statusKey vid) := by
    rw [hS'Eq, applyAll_of_not_touches (validEdit_not_touches_status hValid)]
  have hEn : ResumeEnabled vid S' := ⟨hDrv', s, hSt'.trans hs, hres⟩
  have hCommit : ValidCommitPoint vid S' := Or.inr (Or.inl ⟨s, hSt'.trans hs, hres⟩)
  exact ⟨hFree, hS'Eq, hDrv', hSt', hEn, hCommit⟩

/-- **Theorem 5(a) (`thm:edit` part (a), `stop_edit_resume_det`)**:
if a thread runs from `T₀` with arbitrary stops and resumes (`ResumableRun`),
stops at `S`, undergoes a lease-disciplined edit `EditorSession vid tok edits S S'`,
and continues from `S'` with arbitrary stops and resumes (`ResumableRun`), then:
1. the pre-edit trace `tr₁` equals the first `tr₁.length` labels `ℓ_1 ... ℓ_k`
   of the uninterrupted trace from `T₀`;
2. `S'` is immediately `ResumeEnabled`;
3. the post-edit trace `tr₂` is a prefix of the uninterrupted trace from `S'`
   alone (the combined trace is `ℓ_1 ... ℓ_k · t` with `t` determined solely
   by `S'`, never re-executing completed steps unless `S'` directs control there);
4. the final store `S_final` satisfies `StoreAgree vid S_final U_m` and
   `StatusRel vid S_final U_m` for commit point `U_m` of `S'`. -/
theorem stop_edit_resume_det {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ S S' S_final : Store Key (Val D)}
    {tok : Token} {edits : List (Write Key (Val D))}
    {tr₁ tr₂ : List (StepLabel E)}
    (hInit : ValidCommitPoint vid T₀)
    (hP₀ : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    (hRun₁ : ResumableRun R vid ιs T₀ tr₁ S)
    (hEdit : EditorSession vid tok edits S S')
    (hPS' : ∀ j Uj, commitPointAt R vid (fun j => ιs (tr₁.length + j)) S' j = some Uj →
      StoreRunnable vid Uj → StepDiscipline vid Uj)
    (hRun₂ : ResumableRun R vid (fun j => ιs (tr₁.length + j)) S' tr₂ S_final) :
    tr₁ = uninterruptedPrefix R vid ιs T₀ tr₁.length ∧
    ResumeEnabled vid S' ∧
    tr₂ = uninterruptedPrefix R vid (fun j => ιs (tr₁.length + j)) S' tr₂.length ∧
    (∀ i (hi : i < tr₂.length),
      uninterruptedLabelAt R vid (fun j => ιs (tr₁.length + j)) S' i = some tr₂[i]) ∧
    ∃ U_m, commitPointAt R vid (fun j => ιs (tr₁.length + j)) S' tr₂.length = some U_m ∧
      StoreAgree vid S_final U_m ∧ StatusRel vid S_final U_m := by
  obtain ⟨htr₁, _, _⟩ := stop_resume_resumption hR hInit hP₀ hRun₁
  obtain ⟨_, _, _, _, hEnS', hCommitS'⟩ := hEdit.props
  obtain ⟨htr₂, hidx₂, U_m, hcp₂, hAg₂, hRel₂⟩ :=
    stop_resume_resumption hR hCommitS' hPS' hRun₂
  exact ⟨htr₁, hEnS', htr₂, hidx₂, U_m, hcp₂, hAg₂, hRel₂⟩

/-! ### Shifting Commit Sequences from `T₀` at Index `k` (`Tk`) and Theorem 5(b) -/

theorem initCommit_of_runnable {vid : Vid} {T : Store Key (Val D)}
    (hRun : StoreRunnable vid T) : initCommit vid T = T := by
  obtain ⟨s, hs, hsRun⟩ := hRun
  have hnotRes : s.isResumable = false := by
    cases s <;> simp_all [Status.isRun, Status.isResumable]
  simp [initCommit, hs, hnotRes]

theorem commitPointAt_shift {R : Runtime D I E} {vid : Vid}
    {ιs : Nat → Nat → I} {T₀ Tk : Store Key (Val D)} {k : Nat}
    (hcp : commitPointAt R vid ιs T₀ k = some Tk)
    (hRunTk : StoreRunnable vid Tk) :
    ∀ j, commitPointAt R vid (fun n => ιs (k + n)) Tk j =
      commitPointAt R vid ιs T₀ (k + j) := by
  intro j
  induction j with
  | zero =>
    simp [commitPointAt, initCommit_of_runnable hRunTk, hcp]
  | succ j ih =>
    rw [Nat.add_succ]
    simp only [commitPointAt, ih]
    rfl

theorem uninterruptedLabelAt_shift {R : Runtime D I E} {vid : Vid}
    {ιs : Nat → Nat → I} {T₀ Tk : Store Key (Val D)} {k : Nat}
    (hcp : commitPointAt R vid ιs T₀ k = some Tk)
    (hRunTk : StoreRunnable vid Tk) (j : Nat) :
    uninterruptedLabelAt R vid (fun n => ιs (k + n)) Tk j =
      uninterruptedLabelAt R vid ιs T₀ (k + j) := by
  simp [uninterruptedLabelAt, commitPointAt_shift hcp hRunTk j]

theorem uninterruptedPrefix_shift {R : Runtime D I E} {vid : Vid}
    {ιs : Nat → Nat → I} {T₀ Tk : Store Key (Val D)} {k : Nat}
    (hcp : commitPointAt R vid ιs T₀ k = some Tk)
    (hRunTk : StoreRunnable vid Tk) :
    ∀ m, uninterruptedPrefix R vid ιs T₀ (k + m) =
      uninterruptedPrefix R vid ιs T₀ k ++
        uninterruptedPrefix R vid (fun n => ιs (k + n)) Tk m := by
  intro m
  induction m with
  | zero => simp [uninterruptedPrefix]
  | succ m ih =>
    rw [Nat.add_succ]
    simp only [uninterruptedPrefix, ← uninterruptedLabelAt_shift hcp hRunTk m, ih]
    cases uninterruptedLabelAt R vid (fun n => ιs (k + n)) Tk m with
    | none => rfl
    | some lbl => dsimp only []; rw [List.append_assoc]

private theorem statusRel_preserve_unchanged' {vid : Vid} {S S' Tk : Store Key (Val D)}
    (hRel : StatusRel vid S Tk) (hSt : S' (.statusKey vid) = S (.statusKey vid)) :
    StatusRel vid S' Tk := by
  rcases hRel with ⟨hRun, hRho, s, hs, hne⟩ | ⟨hOkT, hOkS⟩
  · exact Or.inl ⟨hRun, hRho, s, hSt.trans hs, hne⟩
  · exact Or.inr ⟨hOkT, hSt.trans hOkS⟩

private theorem resumeStore_status_and_rho' (vid : Vid) (T : Store Key (Val D)) :
    resumeStore vid T (.statusKey vid) = some (.status .running) ∧
    resumeStore vid T (.stopKey vid) = none := by
  unfold resumeStore
  simp [Write.apply, Write.affects, Write.newVal]

private theorem pauseAfterGate_avoids_rho' (vid : Vid) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRho vid) (pauseAfterGate (E := E) vid) ι T := by
  unfold pauseAfterGate
  refine ExecAvoids.wr_bind ?_ (ExecAvoids.ret _ _ _)
  intro k hk hRho
  simp only [Write.affects, decide_eq_true_eq] at hk
  subst hk
  cases hRho

private theorem finalizeStep_avoids_rho' (vid : Vid) (res : Except ErrMsg (PC × List E))
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRho vid) (finalizeStep vid res) ι T := by
  cases res with
  | ok p =>
    rcases p with ⟨_, _⟩
    exact ExecAvoids.ret _ _ _
  | error _ =>
    unfold finalizeStep
    refine ExecAvoids.wr_bind ?_ (ExecAvoids.wr_bind ?_ (ExecAvoids.ret _ _ _))
    · intro k hk hRho
      simp only [Write.affects, decide_eq_true_eq] at hk
      subst hk; cases hRho
    · intro k hk hRho
      simp only [Write.affects, decide_eq_true_eq] at hk
      subst hk; cases hRho

/-- Direct simulation of `ResumableRun` from an edited store `S'` against `Tk`'s
uninterrupted commit sequence when no key in `Diff` is exposed up to `tr₂.length`
(requiring `StepDiscipline` **only** on `Tk`'s commit sequence, not on `S'`). -/
theorem resumableRun_unread_sim {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {Diff : Key → Prop} {ιs : Nat → Nat → I}
    {Tk S' : Store Key (Val D)}
    (hRunTk : StoreRunnable vid Tk)
    (hrhoTk : Tk (.stopKey vid) = none)
    (hStS' : ∃ s, S' (.statusKey vid) = some (.status s) ∧ s ≠ .ok)
    (hOut0 : ∀ k, ¬ Diff k → ¬ IgnoredByAgree vid (liveDepthOf vid Tk) k →
      Tk k = S' k)
    (hPTk : ∀ j T_kj, commitPointAt R vid ιs Tk j = some T_kj →
      StoreRunnable vid T_kj → StepDiscipline vid T_kj)
    {tr₂ : List (StepLabel E)} {S_final : Store Key (Val D)}
    (hRun₂ : ResumableRun R vid ιs S' tr₂ S_final) :
    (∀ j, j ≤ tr₂.length → ∀ k, Diff k → ¬ stepExposedAt R vid ιs Tk j k) →
    tr₂ = uninterruptedPrefix R vid ιs Tk tr₂.length ∧
    (∀ i (hi : i < tr₂.length), uninterruptedLabelAt R vid ιs Tk i = some tr₂[i]) ∧
    ∃ T_km, commitPointAt R vid ιs Tk tr₂.length = some T_km ∧
      AgreeOutside vid (fun k => Diff k ∧ writtenBefore R vid ιs Tk tr₂.length k = false)
        T_km S_final ∧
      StatusRel vid S_final T_km := by
  induction hRun₂ with
  | init =>
    intro hx
    have hInitTk : initCommit vid Tk = Tk := initCommit_of_runnable hRunTk
    have hcp0 : commitPointAt R vid ιs Tk 0 = some Tk := by
      simp [commitPointAt, hInitTk]
    have hpcNotDiff : ¬ Diff (.pcKey vid) := by
      intro hd
      have hexp0 : stepExposedAt R vid ιs Tk 0 (.pcKey vid) :=
        ⟨rfl, Tk, hcp0, hRunTk, stepCore_exposes_pcKey R vid (ιs 0) Tk⟩
      exact hx 0 (Nat.le_refl 0) (.pcKey vid) hd hexp0
    have hpcEq : Tk (.pcKey vid) = S' (.pcKey vid) :=
      hOut0 (.pcKey vid) hpcNotDiff (pcKey_not_ignored vid _)
    have hAgS' : AgreeOutside vid
        (fun k => Diff k ∧ writtenBefore R vid ιs Tk 0 k = false) Tk S' :=
      ⟨hpcEq, fun k hkNot hkIg => hOut0 k (fun hd => hkNot ⟨hd, rfl⟩) hkIg⟩
    obtain ⟨s0, hs0, hne0⟩ := hStS'
    exact ⟨rfl, by intro i hi; simp at hi, Tk, hcp0, hAgS',
      Or.inl ⟨hRunTk, hrhoTk, s0, hs0, hne0⟩⟩
  | @stop tr S _ ih =>
    intro hx
    obtain ⟨htr, hidx, T_km, hcp, hAg, hRel⟩ := ih hx
    have hSt : (stopWrite vid).apply S (.statusKey vid) = S (.statusKey vid) := by
      simp [stopWrite, Write.apply, Write.affects]
    exact ⟨htr, hidx, T_km, hcp,
      hAg.trans_storeAgree_right (storeAgree_stopWrite vid S).symm,
      statusRel_preserve_unchanged' hRel hSt⟩
  | @leaseAcquire tr S tok _ ih =>
    intro hx
    obtain ⟨htr, hidx, T_km, hcp, hAg, hRel⟩ := ih hx
    cases hDrv : S (.driverKey vid) with
    | none =>
      rw [createIfAbsent_absent _ hDrv]
      have hSt : (Write.put (.driverKey vid) (.lease tok)).apply S (.statusKey vid) =
          S (.statusKey vid) := by simp [Write.apply, Write.affects]
      exact ⟨htr, hidx, T_km, hcp,
        hAg.trans_storeAgree_right (storeAgree_leasePut vid tok S).symm,
        statusRel_preserve_unchanged' hRel hSt⟩
    | some v =>
      rw [createIfAbsent_present _ v hDrv]
      exact ⟨htr, hidx, T_km, hcp, hAg, hRel⟩
  | @leaseRelease tr S _ ih =>
    intro hx
    obtain ⟨htr, hidx, T_km, hcp, hAg, hRel⟩ := ih hx
    have hSt : (Write.del (.driverKey vid)).apply S (.statusKey vid) = S (.statusKey vid) := by
      simp [Write.apply, Write.affects]
    exact ⟨htr, hidx, T_km, hcp,
      hAg.trans_storeAgree_right (storeAgree_leaseDel vid S).symm,
      statusRel_preserve_unchanged' hRel hSt⟩
  | @resume tr S midStops _ hEn ih =>
    intro hx
    obtain ⟨htr, hidx, T_km, hcp, hAg, hRel⟩ := ih hx
    obtain ⟨_, sS, hsS, hresS⟩ := hEn
    have hsNeOk : sS ≠ .ok := by rintro rfl; simp at hresS
    have hTkmRun : StoreRunnable vid T_km ∧ T_km (.stopKey vid) = none := by
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
    have hAgSRes : StoreAgree vid S (applyStops vid midStops (resumeStore vid S)) :=
      (storeAgree_resumeStore vid S).symm.trans (storeAgree_applyStops vid midStops _).symm
    have hSt' : applyStops vid midStops (resumeStore vid S) (.statusKey vid) =
        some (.status .running) := by
      rw [applyStops_other_key vid midStops _ (by intro h; cases h)]
      exact (resumeStore_status_and_rho' vid S).1
    exact ⟨htr, hidx, T_km, hcp, hAg.trans_storeAgree_right hAgSRes,
      Or.inl ⟨hTkmRun.1, hTkmRun.2, .running, hSt', by decide⟩⟩
  | @gatedInFlight tr S S_next ι mas out _ hRunS hIn hexec ih =>
    intro hx
    have hLenMono : tr.length ≤
        (match out.label? with | some lbl => tr ++ [lbl] | none => tr).length := by
      cases out.label? <;> simp
    obtain ⟨htr, hidx, T_kj, hcp, hAg, hRel⟩ :=
      ih (fun j hj => hx j (Nat.le_trans hj hLenMono))
    obtain ⟨sS, hsS, hsRun⟩ := hRunS
    have hsNeOk : sS ≠ .ok := by rintro rfl; simp at hsRun
    have hTkjRun : StoreRunnable vid T_kj ∧ T_kj (.stopKey vid) = none := by
      rcases hRel with ⟨hR_Tk, hRho_Tk, _⟩ | ⟨_, hOkS⟩
      · exact ⟨hR_Tk, hRho_Tk⟩
      · rw [hsS] at hOkS; cases hOkS; exact absurd rfl hsNeOk
    cases hrhoS : S (.stopKey vid) with
    | some v =>
      rw [hrhoS] at hexec
      have havPause := pauseAfterGate_avoids_rho' (D := D) (E := E) vid ι S
      obtain ⟨hout, _, hSNextEq⟩ :=
        validActionExec_right_mover_aux hexec (n := 0) rfl havPause
      rw [Nat.zero_add] at hSNextEq
      have houtPause : out = .paused := by
        simpa [gatedContinuation, pauseAfterGate] using hout
      subst houtPause
      simp only [StepOutcome.label?]
      have hAfPause : after (gatedContinuation R vid (some v)) ι S =
          (Write.put (.statusKey vid) (.status .paused)).apply S := by
        simp [gatedContinuation, pauseAfterGate, after]
      rw [hAfPause] at hSNextEq
      have hAgSNext : StoreAgree vid S S_next := by
        rw [hSNextEq]
        exact (storeAgree_putStatus vid .paused S).symm.trans
          (storeAgree_applyStops vid (MicroAct.stopCount mas) _).symm
      have hSt' : S_next (.statusKey vid) = some (.status .paused) := by
        rw [hSNextEq, applyStops_other_key vid _ _ (by intro h; cases h)]
        simp [Write.apply, Write.affects, Write.newVal]
      exact ⟨htr, hidx, T_kj, hcp, hAg.trans_storeAgree_right hAgSNext,
        Or.inl ⟨hTkjRun.1, hTkjRun.2, .paused, hSt', by decide⟩⟩
    | none =>
      have hι : ι = ιs tr.length := hIn hrhoS
      subst hι
      rw [hrhoS] at hexec
      have hPTkj : StepDiscipline vid T_kj := hPTk tr.length T_kj hcp hTkjRun.1
      have hxj : ∀ k, (Diff k ∧ writtenBefore R vid ιs Tk tr.length k = false) →
          Act.exposed (exec (stepCore R vid) (ιs tr.length) T_kj).2 k = false := by
        rintro k ⟨hd, hwb⟩
        cases hexp : Act.exposed (exec (stepCore R vid) (ιs tr.length) T_kj).2 k with
        | false => rfl
        | true =>
          exact absurd ⟨hwb, T_kj, hcp, hTkjRun.1, hexp⟩
            (hx tr.length hLenMono k hd)
      obtain ⟨hCoreEq, hExecEq, hErrPost, hOkPost⟩ :=
        gatedStep_unread (E := E) hR hAg.2 hTkjRun.2 hrhoS hPTkj (ιs tr.length) hxj
      -- Right-mover reduction on `S` using `hCoreEq` + `hPTkj` (no `StepDiscipline vid S` needed)
      have havCoreTkj := stepCore_avoids_rho_lambda (E := E) hR hPTkj (ιs tr.length)
      have havCoreS : ExecAvoids (IsRho vid) (IsRho vid)
          (stepCore R vid) (ιs tr.length) S := by
        refine ⟨?_, ?_⟩
        · intro k v hm; rw [hCoreEq] at hm; exact havCoreTkj.1 k v hm
        · intro w hm k hk hr; rw [hCoreEq] at hm; exact havCoreTkj.2 w hm k hk (Or.inl hr)
      have havContS : ExecAvoids (IsRho vid) (IsRho vid)
          (gatedContinuation R vid none) (ιs tr.length) S := by
        unfold gatedContinuation
        exact ExecAvoids.bind_op havCoreS (finalizeStep_avoids_rho' vid _ _ _)
      obtain ⟨hout0, _, hSNextEq0⟩ :=
        validActionExec_right_mover_aux hexec (n := 0) rfl havContS
      rw [Nat.zero_add] at hSNextEq0
      have hout : out = (exec (gatedStep R vid) (ιs tr.length) S).1 := by
        rw [gatedStep_eq_read_continuation, exec_read, hrhoS]; exact hout0
      have hSNextEq : S_next =
          applyStops vid (MicroAct.stopCount mas) (after (gatedStep R vid) (ιs tr.length) S) := by
        rw [gatedStep_eq_read_continuation, after_read, hrhoS]; exact hSNextEq0
      have hRunBoolTkj : isStoreRunnableBool vid T_kj = true :=
        (isStoreRunnableBool_iff vid T_kj).2 hTkjRun.1
      cases hCoreTkj : exec (stepCore R vid) (ιs tr.length) T_kj with
      | mk res asCore =>
        have hCoreS : exec (stepCore R vid) (ιs tr.length) S = (res, asCore) :=
          hCoreEq.trans hCoreTkj
        cases res with
        | error err =>
          have houtFail : out = .failed err := by
            rw [hout]
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoS]
            try dsimp only []
            rw [exec_bind]; try dsimp only []
            rw [hCoreS]; dsimp only [finalizeStep]
            simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec]
          subst houtFail
          simp only [StepOutcome.label?]
          have hFailTkj : (exec (stepCore R vid) (ιs tr.length) T_kj).1 = .error err := by
            rw [hCoreTkj]
          have hFailS : (exec (stepCore R vid) (ιs tr.length) S).1 = .error err := by
            rw [hCoreS]
          have hAgRaw := hErrPost err hFailTkj
          have hATkj := failed_gatedStep_preserves_agree hR hTkjRun.2 hFailTkj
          have hAgNext : AgreeOutside vid
              (fun k => Diff k ∧ writtenBefore R vid ιs Tk tr.length k = false)
              T_kj S_next := by
            rw [hSNextEq]
            exact (hAgRaw.trans_storeAgree_left hATkj).trans_storeAgree_right
              (storeAgree_applyStops vid _ _).symm
          have hSt' : S_next (.statusKey vid) = some (.status .error) := by
            rw [hSNextEq, applyStops_other_key vid _ _ (by intro h; cases h)]
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoS]
            try dsimp only []
            rw [after_bind, hFailS]
            dsimp only [finalizeStep]
            simp [Prog.bind_eq, Prog.wr_bind, after_write, Write.apply, Write.affects, Write.newVal]
          exact ⟨htr, hidx, T_kj, hcp, hAgNext,
            Or.inl ⟨hTkjRun.1, hTkjRun.2, .error, hSt', by decide⟩⟩
        | ok pair =>
          rcases pair with ⟨pi', evs⟩
          let lbl : StepLabel E := ⟨pi', evs⟩
          have houtStep : out = .stepped pi' evs := by
            rw [hout]
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoS]
            try dsimp only []
            rw [exec_bind]; try dsimp only []
            rw [hCoreS]; rfl
          subst houtStep
          simp only [StepOutcome.label?]
          have hGatedTkj : (exec (gatedStep R vid) (ιs tr.length) T_kj).1 = .stepped pi' evs := by
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hTkjRun.2]
            try dsimp only []
            rw [exec_bind]; try dsimp only []
            rw [hCoreTkj]; rfl
          have hLblAt : uninterruptedLabelAt R vid ιs Tk tr.length = some lbl := by
            simp [uninterruptedLabelAt, hcp, hRunBoolTkj, hGatedTkj, StepOutcome.label?, lbl]
          let T_raw := after (gatedStep R vid) (ιs tr.length) T_kj
          let Tkj1 := autoResumePaused vid T_raw
          have hcp1 : commitPointAt R vid ιs Tk (tr ++ [lbl]).length = some Tkj1 := by
            simp [commitPointAt, hcp, hRunBoolTkj, hGatedTkj, Tkj1, T_raw]
          have htr1 : tr ++ [lbl] = uninterruptedPrefix R vid ιs Tk (tr ++ [lbl]).length := by
            simp only [List.length_append, List.length_singleton, uninterruptedPrefix, hLblAt]
            exact congrArg (· ++ [lbl]) htr
          have hidx1 : ∀ i (hi : i < (tr ++ [lbl]).length),
              uninterruptedLabelAt R vid ιs Tk i = some (tr ++ [lbl])[i] := by
            intro i hi
            have hi' : i < tr.length + 1 := by simpa using hi
            rcases Nat.lt_or_eq_of_le (Nat.lt_succ_iff.1 hi') with hlt | rfl
            · rw [List.getElem_append_left hlt]
              exact hidx i hlt
            · simp [hLblAt]
          have hOkTkj : (exec (stepCore R vid) (ιs tr.length) T_kj).1 = .ok (pi', evs) := by
            rw [hCoreTkj]
          have hAgRaw := hOkPost pi' evs hOkTkj
          have hWbEq : ∀ k,
              ((Diff k ∧ writtenBefore R vid ιs Tk tr.length k = false) ∧
                touches (Act.writes (exec (stepCore R vid) (ιs tr.length) T_kj).2) k = false) ↔
              (Diff k ∧ writtenBefore R vid ιs Tk (tr ++ [lbl]).length k = false) := by
            intro k
            simp [writtenBefore, hcp, stepWriteTouches, hRunBoolTkj, hCoreTkj, and_assoc]
          have hAgRaw' : AgreeOutside vid
              (fun k => Diff k ∧ writtenBefore R vid ιs Tk (tr ++ [lbl]).length k = false)
              T_raw (after (gatedStep R vid) (ιs tr.length) S) := by
            refine ⟨hAgRaw.1, ?_⟩
            intro k hNot hkIg
            exact hAgRaw.2 k (fun h => hNot ((hWbEq k).1 h)) hkIg
          have hAg1 : AgreeOutside vid
              (fun k => Diff k ∧ writtenBefore R vid ιs Tk (tr ++ [lbl]).length k = false)
              Tkj1 S_next := by
            rw [hSNextEq]
            exact (hAgRaw'.trans_storeAgree_left
              (storeAgree_autoResumePaused vid T_raw).symm).trans_storeAgree_right
              (storeAgree_applyStops vid _ _).symm
          obtain ⟨_, _, _, _, _, hStCases⟩ := stepCore_ok_post hR hPTkj hCoreTkj
          let ws := Act.writes asCore
          have hSRawEq : after (gatedStep R vid) (ιs tr.length) S = applyAll S ws := by
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoS]
            rw [after_bind]; unfold after at *; rw [hCoreS]; rfl
          have hTRawEq : T_raw = applyAll T_kj ws := by
            show after (gatedStep R vid) (ιs tr.length) T_kj = applyAll T_kj ws
            unfold gatedStep
            simp only [Prog.bind_eq, Prog.rd_bind, after_read, hTkjRun.2]
            rw [after_bind]; unfold after at *; rw [hCoreTkj]; rfl
          have hNotTouchRho : touches ws (.stopKey vid) = false := by
            have ht := havCoreTkj.not_touches (Or.inl rfl : IsRhoOrLambda vid (.stopKey vid))
            rw [hCoreTkj] at ht; exact ht
          have hRhoRaw : T_raw (.stopKey vid) = none := by
            rw [hTRawEq, applyAll_of_not_touches hNotTouchRho, hTkjRun.2]
          have hSNextStatus : S_next (.statusKey vid) = applyAll S ws (.statusKey vid) := by
            rw [hSNextEq, applyStops_other_key vid _ _ (by intro h; cases h), hSRawEq]
          refine ⟨htr1, hidx1, Tkj1, hcp1, hAg1, ?_⟩
          cases ht : touches ws (.statusKey vid) with
          | false =>
            obtain ⟨sT, hsT, hsTRun⟩ := hTkjRun.1
            have hSNextSt : S_next (.statusKey vid) = some (.status sS) := by
              rw [hSNextStatus, applyAll_of_not_touches ht, hsS]
            have hTRawSt : T_raw (.statusKey vid) = some (.status sT) := by
              rw [hTRawEq, applyAll_of_not_touches ht, hsT]
            have hsTNeP : sT ≠ .paused := by rintro rfl; simp at hsTRun
            have hTkj1Eq : Tkj1 = T_raw := by simp [Tkj1, autoResumePaused, hTRawSt, hsTNeP]
            rw [hTkj1Eq]
            exact Or.inl ⟨⟨sT, hTRawSt, hsTRun⟩, hRhoRaw, sS, hSNextSt, hsNeOk⟩
          | true =>
            rcases hStCases with hUntouched | ⟨s', hs'Raw, hs'NeErr⟩
            · rw [ht] at hUntouched; cases hUntouched
            · have hSNextSt : S_next (.statusKey vid) = some (.status s') := by
                rw [hSNextStatus, applyAll_eq_of_touches S T_kj ht, hs'Raw]
              have hTRawSt : T_raw (.statusKey vid) = some (.status s') := by
                rw [hTRawEq, hs'Raw]
              cases s' with
              | error => exact absurd rfl hs'NeErr
              | ok =>
                have hTkj1Eq : Tkj1 = T_raw := by simp [Tkj1, autoResumePaused, hTRawSt]
                rw [hTkj1Eq]; exact Or.inr ⟨hTRawSt, hSNextSt⟩
              | paused =>
                have hTkj1Eq : Tkj1 = resumeStore vid T_raw := by
                  simp [Tkj1, autoResumePaused, hTRawSt]
                rw [hTkj1Eq]
                obtain ⟨hStRes, hRhoRes⟩ := resumeStore_status_and_rho' vid T_raw
                exact Or.inl ⟨⟨.running, hStRes, rfl⟩, hRhoRes, .paused, hSNextSt, by decide⟩
              | init =>
                have hTkj1Eq : Tkj1 = T_raw := by simp [Tkj1, autoResumePaused, hTRawSt]
                rw [hTkj1Eq]
                exact Or.inl ⟨⟨.init, hTRawSt, rfl⟩, hRhoRaw, .init, hSNextSt, by decide⟩
              | running =>
                have hTkj1Eq : Tkj1 = T_raw := by simp [Tkj1, autoResumePaused, hTRawSt]
                rw [hTkj1Eq]
                exact Or.inl ⟨⟨.running, hTRawSt, rfl⟩, hRhoRaw, .running, hSNextSt, by decide⟩
              | wait =>
                have hTkj1Eq : Tkj1 = T_raw := by simp [Tkj1, autoResumePaused, hTRawSt]
                rw [hTkj1Eq]
                exact Or.inl ⟨⟨.wait, hTRawSt, rfl⟩, hRhoRaw, .wait, hSNextSt, by decide⟩

/-- **Theorem 5(b) Sharp Prefix Form (`stop_edit_resume_unread_prefix`)**:
if `ResumableRun` stops at `S`, undergoes `EditorSession vid tok edits S S'`,
and continues via `ResumableRun` emitting `tr₂` to `S_final`, and no key touched
by `edits` is exposed at any step index `j ≤ tr₂.length` from `Tk` (assuming
`StepDiscipline` **only** along `T₀`'s uninterrupted commit sequence), then:
1. the combined trace `tr₁ ++ tr₂` equals the `(tr₁.length + tr₂.length)`-prefix
   of the **original unedited** uninterrupted trace from `T₀` (the edit cannot
   be observed);
2. every index `i < (tr₁ ++ tr₂).length` matches `uninterruptedLabelAt R vid ιs T₀ i`;
3. `S_final` agrees with commit point `T_{k+m}` of `T₀` outside `IgnoredByAgree`
   and unoverwritten keys of `edits` (and satisfies full `StoreAgree` if every
   edited key was overwritten). -/
theorem stop_edit_resume_unread_prefix {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ S S' S_final : Store Key (Val D)}
    {tok : Token} {edits : List (Write Key (Val D))}
    {tr₁ tr₂ : List (StepLabel E)}
    (hInit : ValidCommitPoint vid T₀)
    (hP₀ : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    (hRun₁ : ResumableRun R vid ιs T₀ tr₁ S)
    (hEdit : EditorSession vid tok edits S S')
    (hRun₂ : ResumableRun R vid (fun j => ιs (tr₁.length + j)) S' tr₂ S_final)
    (hUnexposed : ∀ Tk, commitPointAt R vid ιs T₀ tr₁.length = some Tk →
      ∀ j, j ≤ tr₂.length → ∀ k, touches edits k = true →
        ¬ stepExposedAt R vid (fun n => ιs (tr₁.length + n)) Tk j k) :
    tr₁ ++ tr₂ = uninterruptedPrefix R vid ιs T₀ (tr₁.length + tr₂.length) ∧
    (∀ i (hi : i < (tr₁ ++ tr₂).length),
      uninterruptedLabelAt R vid ιs T₀ i = some (tr₁ ++ tr₂)[i]) ∧
    ∃ Tk T_km,
      commitPointAt R vid ιs T₀ tr₁.length = some Tk ∧
      commitPointAt R vid ιs T₀ (tr₁.length + tr₂.length) = some T_km ∧
      AgreeOutside vid
        (fun k => touches edits k = true ∧
          writtenBefore R vid (fun n => ιs (tr₁.length + n)) Tk tr₂.length k = false)
        T_km S_final ∧
      StatusRel vid S_final T_km := by
  obtain ⟨htr₁, hidx₁, Tk, hcpTk, hAgS, hRelS⟩ :=
    stop_resume_resumption hR hInit hP₀ hRun₁
  obtain ⟨_, hS'Eq, _, hStEq, _, _⟩ := hEdit.props
  obtain ⟨_, ⟨sS, hsS, hresS⟩, _⟩ := hEdit
  have hsSNeOk : sS ≠ .ok := by rintro rfl; simp at hresS
  have hTkRun : StoreRunnable vid Tk ∧ Tk (.stopKey vid) = none := by
    rcases hRelS with ⟨hR_Tk, hRho_Tk, _⟩ | ⟨_, hOkS⟩
    · exact ⟨hR_Tk, hRho_Tk⟩
    · rw [hsS] at hOkS; cases hOkS; exact absurd rfl hsSNeOk
  have hOut0 : ∀ k, ¬ (touches edits k = true) →
      ¬ IgnoredByAgree vid (liveDepthOf vid Tk) k → Tk k = S' k := by
    intro k hNotTouch hkIg
    have htFalse : touches edits k = false := by
      cases ht : touches edits k with
      | false => rfl
      | true => exact absurd ht hNotTouch
    rw [hS'Eq, applyAll_of_not_touches htFalse]
    exact (hAgS.symm.2 k hkIg)
  have hPTk : ∀ j T_kj,
      commitPointAt R vid (fun n => ιs (tr₁.length + n)) Tk j = some T_kj →
      StoreRunnable vid T_kj → StepDiscipline vid T_kj := by
    intro j T_kj hcpj hRunj
    rw [commitPointAt_shift hcpTk hTkRun.1 j] at hcpj
    exact hP₀ (tr₁.length + j) T_kj hcpj hRunj
  obtain ⟨htr₂, hidx₂, T_km, hcpKm, hAgFinal, hRelFinal⟩ :=
    resumableRun_unread_sim hR hTkRun.1 hTkRun.2 ⟨sS, hStEq.trans hsS, hsSNeOk⟩
      hOut0 hPTk hRun₂ (hUnexposed Tk hcpTk)
  have hComb : tr₁ ++ tr₂ = uninterruptedPrefix R vid ιs T₀ (tr₁.length + tr₂.length) := by
    rw [uninterruptedPrefix_shift hcpTk hTkRun.1 tr₂.length, ← htr₁, ← htr₂]
  have hCombIdx : ∀ i (hi : i < (tr₁ ++ tr₂).length),
      uninterruptedLabelAt R vid ιs T₀ i = some (tr₁ ++ tr₂)[i] := by
    intro i hi
    rw [List.length_append] at hi
    by_cases hlt : i < tr₁.length
    · rw [List.getElem_append_left hlt]
      exact hidx₁ i hlt
    · have hge : tr₁.length ≤ i := by omega
      have hrem : i - tr₁.length < tr₂.length := by omega
      have hiSplit : i = tr₁.length + (i - tr₁.length) := by omega
      rw [List.getElem_append_right hge]
      have h2 := hidx₂ (i - tr₁.length) hrem
      rw [uninterruptedLabelAt_shift hcpTk hTkRun.1 (i - tr₁.length), ← hiSplit] at h2
      exact h2
  rw [commitPointAt_shift hcpTk hTkRun.1 tr₂.length] at hcpKm
  exact ⟨hComb, hCombIdx, Tk, T_km, hcpTk, hcpKm, hAgFinal, hRelFinal⟩

/-- **Theorem 5(b) (`thm:edit` part (b), `stop_edit_resume_unread`)**:
paper statement quantifying over all steps `j : Nat` of the remaining
uninterrupted run from `Tk`. -/
theorem stop_edit_resume_unread {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ S S' S_final : Store Key (Val D)}
    {tok : Token} {edits : List (Write Key (Val D))}
    {tr₁ tr₂ : List (StepLabel E)}
    (hInit : ValidCommitPoint vid T₀)
    (hP₀ : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    (hRun₁ : ResumableRun R vid ιs T₀ tr₁ S)
    (hEdit : EditorSession vid tok edits S S')
    (hRun₂ : ResumableRun R vid (fun j => ιs (tr₁.length + j)) S' tr₂ S_final)
    (hUnexposed : ∀ Tk, commitPointAt R vid ιs T₀ tr₁.length = some Tk →
      ∀ j k, touches edits k = true →
        ¬ stepExposedAt R vid (fun n => ιs (tr₁.length + n)) Tk j k) :
    tr₁ ++ tr₂ = uninterruptedPrefix R vid ιs T₀ (tr₁.length + tr₂.length) ∧
    (∀ i (hi : i < (tr₁ ++ tr₂).length),
      uninterruptedLabelAt R vid ιs T₀ i = some (tr₁ ++ tr₂)[i]) ∧
    ∃ Tk T_km,
      commitPointAt R vid ιs T₀ tr₁.length = some Tk ∧
      commitPointAt R vid ιs T₀ (tr₁.length + tr₂.length) = some T_km ∧
      AgreeOutside vid
        (fun k => touches edits k = true ∧
          writtenBefore R vid (fun n => ιs (tr₁.length + n)) Tk tr₂.length k = false)
        T_km S_final ∧
      StatusRel vid S_final T_km :=
  stop_edit_resume_unread_prefix hR hInit hP₀ hRun₁ hEdit hRun₂
    (fun Tk hTk j _ k hk => hUnexposed Tk hTk j k hk)

end StopEditResume

/-! ## T4c: Compositional Live-Edit Splicing (C1) and Coordinate Relocation (C2) -/

section LiveEditSimulation

variable {D I E : Type}

/-- Any key outside `sigma` and `rho` that is not written before step `j`
retains its `initCommit` value at commit point `Uj`. -/
theorem commitPointAt_eq_init_of_not_writtenBefore {R : Runtime D I E}
    {vid : Vid} {ιs : Nat → Nat → I} {U : Store Key (Val D)} :
    ∀ {j : Nat} {Uj : Store Key (Val D)} {k : Key},
      commitPointAt R vid ιs U j = some Uj →
      writtenBefore R vid ιs U j k = false →
      k ≠ .statusKey vid →
      k ≠ .stopKey vid →
      Uj k = initCommit vid U k
  | 0, Uj, k, hcp, _, _, _ => by
      simp only [commitPointAt, Option.some.injEq] at hcp
      rw [← hcp]
  | j + 1, Uj1, k, hcp1, hwb1, hkSt, hkRho => by
      simp only [commitPointAt] at hcp1
      cases hcpj : commitPointAt R vid ιs U j with
      | none => rw [hcpj] at hcp1; simp at hcp1
      | some Uj =>
        rw [hcpj] at hcp1
        dsimp only [] at hcp1
        cases hRunBool : isStoreRunnableBool vid Uj with
        | false => rw [hRunBool] at hcp1; simp at hcp1
        | true =>
          simp only [hRunBool, ite_true] at hcp1
          cases hGated : (exec (gatedStep R vid) (ιs j) Uj).1 with
          | paused => rw [hGated] at hcp1; simp at hcp1
          | failed _ => rw [hGated] at hcp1; simp at hcp1
          | stepped pi' evs =>
            rw [hGated] at hcp1
            simp only [Option.some.injEq] at hcp1
            have hrhoUj : Uj (.stopKey vid) = none := by
              cases hr : Uj (.stopKey vid) with
              | none => rfl
              | some _ =>
                have hp : (exec (gatedStep R vid) (ιs j) Uj).1 = .paused := by
                  simp [gatedStep, pauseAfterGate, hr]
                rw [hGated] at hp; cases hp
            have hCoreOk : (exec (stepCore R vid) (ιs j) Uj).1 = .ok (pi', evs) := by
              have hg := hGated
              unfold gatedStep at hg
              simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hrhoUj] at hg
              try dsimp only [] at hg
              rw [exec_bind] at hg
              dsimp only [] at hg
              cases hc : (exec (stepCore R vid) (ιs j) Uj).1 with
              | error _ =>
                rw [hc] at hg; dsimp only [finalizeStep] at hg
                simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hg
              | ok pair =>
                rcases pair with ⟨p1, e1⟩
                rw [hc] at hg
                simp [finalizeStep] at hg
                rcases hg with ⟨rfl, rfl⟩
                rfl
            simp only [writtenBefore, hcpj, stepWriteTouches, hRunBool, ite_true,
              hCoreOk, Bool.or_eq_false_iff] at hwb1
            have hAfterEq : after (gatedStep R vid) (ιs j) Uj =
                applyAll Uj (Act.writes (exec (stepCore R vid) (ιs j) Uj).2) := by
              unfold gatedStep
              simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrhoUj]
              rw [after_bind]
              cases hcFull : exec (stepCore R vid) (ιs j) Uj with
              | mk res asCore =>
                have hres : res = .ok (pi', evs) := by rw [hcFull] at hCoreOk; exact hCoreOk
                subst hres
                unfold after at *
                rw [hcFull]; rfl
            have hAutoOther : ∀ (S : Store Key (Val D)), autoResumePaused vid S k = S k := by
              intro S
              unfold autoResumePaused
              cases S (.statusKey vid) with
              | none => rfl
              | some v =>
                cases v <;> try rfl
                rename_i s
                cases s <;> try rfl
                simp [resumeStore, Write.apply, Write.affects, hkSt, hkRho]
            rw [← hcp1, hAutoOther, hAfterEq, applyAll_of_not_touches hwb1.2]
            exact commitPointAt_eq_init_of_not_writtenBefore hcpj hwb1.1 hkSt hkRho

/-- **T4c.1 Compositional Live-Edit Splicing Theorem (`live_edit_spliced_program_equiv`)**:
models `edit-tool` Case C1 (`06-control.tex` line 61). Suppose `T₀` is runnable
with `rho = none`, `edits` (`ValidEdit vid edits`, not touching `kappa`) modifies
only paths (for example `/lib/fn/[r]` for `r ≥ r₀`) that are neither exposed nor
written during the first `k = tr₁.length` steps of `T₀`. If a thread runs `tr₁`
from `T₀`, stops at `S`, applies `EditorSession vid tok edits S S'` in place,
and continues via `ResumableRun` emitting `tr₂` to `S_final`, then:
1. the combined trace `tr₁ ++ tr₂` equals the `(k + m)`-prefix of the
   uninterrupted trace from the **spliced initial store** `applyAll T₀ edits`
   (achieving the result of running under the new code without re-executing `tr₁`);
2. `S_final` is `StoreAgree`-equivalent (`~`) and `StatusRel`-related to the
   `(k + m)`-th commit point of `applyAll T₀ edits`. -/
theorem live_edit_spliced_program_equiv {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ S S' S_final : Store Key (Val D)}
    {tok : Token} {edits : List (Write Key (Val D))}
    {tr₁ tr₂ : List (StepLabel E)}
    (hRun₀ : StoreRunnable vid T₀)
    (hrho₀ : T₀ (.stopKey vid) = none)
    (hpcEdit : touches edits (.pcKey vid) = false)
    (hP₀ : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    (hPSpliced : ∀ k Uk, commitPointAt R vid ιs (applyAll T₀ edits) k = some Uk →
      StoreRunnable vid Uk → StepDiscipline vid Uk)
    (hRun₁ : ResumableRun R vid ιs T₀ tr₁ S)
    (hEdit : EditorSession vid tok edits S S')
    (hUnexpPre : ∀ j, j < tr₁.length → ∀ key, touches edits key = true →
      ¬ stepExposedAt R vid ιs T₀ j key)
    (hUnwrPre : ∀ key, touches edits key = true →
      writtenBefore R vid ιs T₀ tr₁.length key = false)
    (hRun₂ : ResumableRun R vid (fun j => ιs (tr₁.length + j)) S' tr₂ S_final) :
    let T_spliced := applyAll T₀ edits
    tr₁ ++ tr₂ = uninterruptedPrefix R vid ιs T_spliced (tr₁.length + tr₂.length) ∧
    (∀ i (hi : i < (tr₁ ++ tr₂).length),
      uninterruptedLabelAt R vid ιs T_spliced i = some (tr₁ ++ tr₂)[i]) ∧
    ∃ U_km,
      commitPointAt R vid ιs T_spliced (tr₁.length + tr₂.length) = some U_km ∧
      StoreAgree vid S_final U_km ∧
      StatusRel vid S_final U_km := by
  let T_spliced := applyAll T₀ edits
  have hEdProps := EditorSession.props hEdit
  have hValid := hEdit.2.2.1
  obtain ⟨_, hS'Eq, _, hStEqS', _, _⟩ := hEdProps
  obtain ⟨_, ⟨sS, hsS, hresS⟩, _⟩ := hEdit
  have hsSNeOk : sS ≠ .ok := by rintro rfl; simp at hresS
  have hRunSpl : StoreRunnable vid T_spliced := by
    obtain ⟨s0, hs0, hs0Run⟩ := hRun₀
    refine ⟨s0, ?_, hs0Run⟩
    dsimp only [T_spliced]
    rw [applyAll_of_not_touches (validEdit_not_touches_status hValid), hs0]
  have hrhoSpl : T_spliced (.stopKey vid) = none := by
    dsimp only [T_spliced]
    rw [applyAll_of_not_touches (validEdit_not_touches_stop hValid), hrho₀]
  have hInit₀ : ValidCommitPoint vid T₀ := Or.inl ⟨hRun₀, hrho₀⟩
  obtain ⟨htr₁, hidx₁, Tk, hcpTk, hAgSTk, hRelSTk⟩ :=
    stop_resume_resumption hR hInit₀ hP₀ hRun₁
  have hTkRun : StoreRunnable vid Tk ∧ Tk (.stopKey vid) = none := by
    rcases hRelSTk with ⟨hR_Tk, hRho_Tk, _⟩ | ⟨_, hOkS⟩
    · exact ⟨hR_Tk, hRho_Tk⟩
    · rw [hsS] at hOkS; cases hOkS; exact absurd rfl hsSNeOk
  -- Establish `Uk` at index `tr₁.length` of `T_spliced`, matching `tr₁` and `S' ~ Uk`
  have hBridge :
      tr₁ = uninterruptedPrefix R vid ιs T_spliced tr₁.length ∧
      (∀ i (hi : i < tr₁.length),
        uninterruptedLabelAt R vid ιs T_spliced i = some tr₁[i]) ∧
      ∃ Uk, commitPointAt R vid ιs T_spliced tr₁.length = some Uk ∧
        StoreAgree vid S' Uk ∧
        StoreRunnable vid Uk ∧
        Uk (.stopKey vid) = none := by
    cases tr₁ with
    | nil =>
      simp only [List.length_nil] at hcpTk ⊢
      simp only [commitPointAt, initCommit_of_runnable hRun₀, Option.some.injEq] at hcpTk
      subst hcpTk
      have hcpSpl0 : commitPointAt R vid ιs T_spliced 0 = some T_spliced := by
        simp [commitPointAt, initCommit_of_runnable hRunSpl]
      have hpcS' : S' (.pcKey vid) = T_spliced (.pcKey vid) := by
        rw [hS'Eq, applyAll_of_not_touches hpcEdit,
            show T_spliced (.pcKey vid) = T₀ (.pcKey vid) from
              applyAll_of_not_touches hpcEdit]
        exact hAgSTk.1
      have hAgS'Spl : StoreAgree vid S' T_spliced := by
        refine ⟨hpcS', ?_⟩
        intro k hkIg
        rw [hS'Eq]
        cases ht : touches edits k with
        | true => exact applyAll_eq_of_touches S T₀ ht
        | false =>
          rw [applyAll_of_not_touches ht,
              show T_spliced k = T₀ k from applyAll_of_not_touches ht]
          have hpcEqS : S' (.pcKey vid) = S (.pcKey vid) := by
            rw [hS'Eq, applyAll_of_not_touches hpcEdit]
          rw [liveDepthOf_eq_of_pc_eq hpcEqS] at hkIg
          exact hAgSTk.2 k hkIg
      exact ⟨rfl, by intro i hi; simp at hi, T_spliced, hcpSpl0, hAgS'Spl, hRunSpl, hrhoSpl⟩
    | cons lbl0 trRest =>
      let kLen := (lbl0 :: trRest).length
      have hkPos : 1 ≤ kLen := by simp [kLen]
      have hOutInit : ∀ k, ¬ (touches edits k = true) →
          ¬ IgnoredByAgree vid (liveDepthOf vid (initCommit vid T₀)) k →
          initCommit vid T₀ k = initCommit vid T_spliced k := by
        intro k hNotTouch _
        rw [initCommit_of_runnable hRun₀, initCommit_of_runnable hRunSpl]
        cases ht : touches edits k with
        | false => exact (applyAll_of_not_touches ht).symm
        | true => exact absurd ht hNotTouch
      obtain ⟨hLblPre, hPrefPre, hStepPre⟩ :=
        uninterrupted_unread (E := E) hR hkPos
          (by rw [initCommit_of_runnable hRun₀]; exact hRun₀)
          (by rw [initCommit_of_runnable hRunSpl]; exact hRunSpl)
          (by rw [initCommit_of_runnable hRun₀]; exact hrho₀)
          (by rw [initCommit_of_runnable hRunSpl]; exact hrhoSpl)
          hOutInit hP₀ hUnexpPre
      obtain ⟨_, hSomePre⟩ := hStepPre kLen (Nat.le_refl _)
      obtain ⟨Uk, hcpUk, hAgTkUk, hRunIffUk, _, hrhoUk⟩ := hSomePre Tk hcpTk
      have hRunUk : StoreRunnable vid Uk := hRunIffUk.1 hTkRun.1
      have hWbSame : ∀ j, j ≤ kLen → ∀ key,
          writtenBefore R vid ιs T_spliced j key = writtenBefore R vid ιs T₀ j key := by
        intro j hjLe key
        induction j with
        | zero => rfl
        | succ j ih =>
          simp only [writtenBefore, ih (by omega)]
          obtain ⟨hNoneJ, hSomeJ⟩ := hStepPre j (by omega)
          cases hcpTj : commitPointAt R vid ιs T₀ j with
          | none =>
            rw [hNoneJ.1 hcpTj]
          | some Tj =>
            obtain ⟨Uj, hcpUj, hAgJ, hRunJ, hrhoTj, hrhoUj⟩ := hSomeJ Tj hcpTj
            rw [hcpUj]
            dsimp only [stepWriteTouches]
            cases hRunBoolT : isStoreRunnableBool vid Tj with
            | false =>
              have hRunBoolU : isStoreRunnableBool vid Uj = false := by
                cases hb : isStoreRunnableBool vid Uj with
                | false => rfl
                | true =>
                  have hrT := (isStoreRunnableBool_iff vid Tj).2
                    (hRunJ.2 ((isStoreRunnableBool_iff vid Uj).1 hb))
                  rw [hRunBoolT] at hrT; cases hrT
              simp [hRunBoolU]
            | true =>
              have hRunTj := (isStoreRunnableBool_iff vid Tj).1 hRunBoolT
              have hRunBoolU := (isStoreRunnableBool_iff vid Uj).2 (hRunJ.1 hRunTj)
              have hPTj := hP₀ j Tj hcpTj hRunTj
              have hxj : ∀ k', (touches edits k' = true ∧ writtenBefore R vid ιs T₀ j k' = false) →
                  Act.exposed (exec (stepCore R vid) (ιs j) Tj).2 k' = false := by
                rintro k' ⟨hd, hwb⟩
                cases hexp : Act.exposed (exec (stepCore R vid) (ιs j) Tj).2 k' with
                | false => rfl
                | true =>
                  exact absurd ⟨hwb, Tj, hcpTj, hRunTj, hexp⟩
                    (hUnexpPre j (by omega) k' hd)
              obtain ⟨hCoreEqJ, _, _, _⟩ :=
                gatedStep_unread (E := E) hR hAgJ.2 hrhoTj hrhoUj hPTj (ιs j) hxj
              simp [hRunBoolU, hCoreEqJ]
      have hpcS'Uk : S' (.pcKey vid) = Uk (.pcKey vid) := by
        rw [hS'Eq, applyAll_of_not_touches hpcEdit]
        exact hAgSTk.1.trans hAgTkUk.1
      have hAgS'Uk : StoreAgree vid S' Uk := by
        refine ⟨hpcS'Uk, ?_⟩
        intro key hkIg
        rw [hS'Eq]
        cases ht : touches edits key with
        | true =>
          have hwbSpl : writtenBefore R vid ιs T_spliced kLen key = false := by
            rw [hWbSame kLen (Nat.le_refl _) key]
            exact hUnwrPre key ht
          have hkNotSt : key ≠ .statusKey vid := by
            rintro rfl
            rw [validEdit_not_touches_status hValid] at ht; cases ht
          have hkNotRho : key ≠ .stopKey vid := by
            rintro rfl
            rw [validEdit_not_touches_stop hValid] at ht; cases ht
          have hUkInit :=
            commitPointAt_eq_init_of_not_writtenBefore hcpUk hwbSpl hkNotSt hkNotRho
          rw [initCommit_of_runnable hRunSpl] at hUkInit
          rw [hUkInit]
          exact applyAll_eq_of_touches S T₀ ht
        | false =>
          rw [applyAll_of_not_touches ht]
          have hpcEqS : S' (.pcKey vid) = S (.pcKey vid) := by
            rw [hS'Eq, applyAll_of_not_touches hpcEdit]
          rw [liveDepthOf_eq_of_pc_eq hpcEqS] at hkIg
          have hkIgTk : ¬ IgnoredByAgree vid (liveDepthOf vid Tk) key := by
            rw [← liveDepthOf_eq_of_pc_eq hAgSTk.1]; exact hkIg
          exact (hAgSTk.2 key hkIg).trans
            (hAgTkUk.2 key (fun ⟨ht', _⟩ => by rw [ht] at ht'; cases ht') hkIgTk)
      refine ⟨htr₁.trans (hPrefPre kLen (Nat.le_refl _)).symm, ?_,
        Uk, hcpUk, hAgS'Uk, hRunUk, hrhoUk⟩
      intro i hi
      rw [hLblPre i hi]
      exact hidx₁ i hi
  obtain ⟨htr₁Spl, hidx₁Spl, Uk, hcpUk, hAgS'Uk, hRunUk, hrhoUk⟩ := hBridge
  -- Simulate `hRun₂` from `S'` against `Uk` (`Diff := False`) via `resumableRun_unread_sim`
  have hPUk : ∀ j U_kj,
      commitPointAt R vid (fun n => ιs (tr₁.length + n)) Uk j = some U_kj →
      StoreRunnable vid U_kj → StepDiscipline vid U_kj := by
    intro j U_kj hcpj hRunj
    rw [commitPointAt_shift hcpUk hRunUk j] at hcpj
    exact hPSpliced (tr₁.length + j) U_kj hcpj hRunj
  have hStS' : ∃ s, S' (.statusKey vid) = some (.status s) ∧ s ≠ .ok :=
    ⟨sS, hStEqS'.trans hsS, hsSNeOk⟩
  obtain ⟨htr₂Spl, hidx₂Spl, U_km, hcpUkm, hAgOutFinal, hRelFinal⟩ :=
    resumableRun_unread_sim (Diff := fun _ => False) hR hRunUk hrhoUk hStS'
      (fun k _ hkIg => (hAgS'Uk.symm.2 k hkIg))
      hPUk hRun₂ (fun _ _ _ hf => False.elim hf)
  have hAgFinal : StoreAgree vid S_final U_km :=
    (AgreeOutside.to_storeAgree_empty
      (hAgOutFinal.mono (fun _ h => h.1))).symm
  have hComb : tr₁ ++ tr₂ = uninterruptedPrefix R vid ιs T_spliced (tr₁.length + tr₂.length) := by
    rw [uninterruptedPrefix_shift hcpUk hRunUk tr₂.length, ← htr₁Spl, ← htr₂Spl]
  have hCombIdx : ∀ i (hi : i < (tr₁ ++ tr₂).length),
      uninterruptedLabelAt R vid ιs T_spliced i = some (tr₁ ++ tr₂)[i] := by
    intro i hi
    rw [List.length_append] at hi
    by_cases hlt : i < tr₁.length
    · rw [List.getElem_append_left hlt]
      exact hidx₁Spl i hlt
    · have hge : tr₁.length ≤ i := by omega
      have hrem : i - tr₁.length < tr₂.length := by omega
      have hiSplit : i = tr₁.length + (i - tr₁.length) := by omega
      rw [List.getElem_append_right hge]
      have h2 := hidx₂Spl (i - tr₁.length) hrem
      rw [uninterruptedLabelAt_shift hcpUk hRunUk (i - tr₁.length), ← hiSplit] at h2
      exact h2
  rw [commitPointAt_shift hcpUk hRunUk tr₂.length] at hcpUkm
  exact ⟨hComb, hCombIdx, U_km, hcpUkm, hAgFinal, hRelFinal⟩

/-! ### T4c.2 Coordinate Relocation Simulation and Unmigrated `returnpc` Counterexample (Case C2) -/

/-- Row-coordinate relocation map for inserting `delta` instructions before row
`r₀` inside `targetFn` (`06-control.tex` lines 66-67, Case C2). -/
def shiftRow (r₀ delta r : Nat) : Nat :=
  if r₀ ≤ r then r + delta else r

/-- Relocate a counter `(d, r)` if frame `d` executes `targetFn`, leaving frames
of all other functions untouched. -/
def relocatePC (vid : Vid) (targetFn : SymName) (r₀ delta : Nat)
    (T : Store Key (Val D)) (p : PC) : PC :=
  match T (.frameLib vid p.depth) with
  | some (.sym fn) =>
      if fn == targetFn then ⟨p.depth, shiftRow r₀ delta p.row⟩ else p
  | _ => p

theorem shiftRow_valid {r₀ delta r : Nat} (hr : 1 ≤ r) :
    1 ≤ shiftRow r₀ delta r := by
  unfold shiftRow
  split <;> omega

theorem relocatePC_valid_and_depth (vid : Vid) (targetFn : SymName) (r₀ delta : Nat)
    (T : Store Key (Val D)) {p : PC} (hp : p.valid = true) :
    (relocatePC vid targetFn r₀ delta T p).valid = true ∧
    (relocatePC vid targetFn r₀ delta T p).depth = p.depth := by
  unfold relocatePC
  cases T (.frameLib vid p.depth) with
  | none => exact ⟨hp, rfl⟩
  | some v =>
    cases v <;> try exact ⟨hp, rfl⟩
    rename_i fn
    dsimp only []
    cases fn == targetFn with
    | false => exact ⟨hp, rfl⟩
    | true =>
      rw [PC.valid_iff] at hp
      exact ⟨(PC.valid_iff _).2 ⟨hp.1, shiftRow_valid hp.2⟩, rfl⟩

/-- **T4c.2 `Return` Coordinate-Relocation Simulation (`return_relocated_step_sim`)**:
when code is inserted into `targetFn` (`shiftRow r₀ delta`) and an editor
migrates both `kappa` and `[d]..returnpc` via `relocatePC`, executing `Return`
at depth `d > 1` from `T` and `T_rel` both succeed and land at `r` and
`relocatePC vid targetFn r₀ delta T r` respectively. -/
theorem return_relocated_step_sim {vid : Vid} {targetFn : SymName}
    {r₀ delta : Nat} {T T_rel : Store Key (Val D)} {pi r : PC}
    (hd : 1 < pi.depth)
    (hrValid : r.valid = true)
    (hrDepth : r.depth + 1 = pi.depth)
    (hLibCaller : T_rel (.frameLib vid r.depth) = T (.frameLib vid r.depth))
    (hRetOrig : T (.frameRetPc vid pi.depth) = some (.pc r))
    (hRetRel : T_rel (.frameRetPc vid pi.depth) =
      some (.pc (relocatePC vid targetFn r₀ delta T r)))
    (ι : Nat → I) :
    let pi_rel := relocatePC vid targetFn r₀ delta T pi
    let r_rel := relocatePC vid targetFn r₀ delta T_rel r
    (exec (returnRuleProg (D := D) (I := I) (E := E) vid pi [] []) ι T).1 = .ok (r, []) ∧
    (exec (returnRuleProg (D := D) (I := I) (E := E) vid pi_rel [] []) ι T_rel).1 =
      .ok (r_rel, []) ∧
    after (returnRuleProg (D := D) (I := I) (E := E) vid pi [] []) ι T (.pcKey vid) =
      some (.pc r) ∧
    after (returnRuleProg (D := D) (I := I) (E := E) vid pi_rel [] []) ι T_rel (.pcKey vid) =
      some (.pc r_rel) := by
  intro pi_rel r_rel
  have hPiRelDepth : pi_rel.depth = pi.depth := by
    unfold pi_rel relocatePC
    cases T (.frameLib vid pi.depth) with
    | none => rfl
    | some v =>
      cases v <;> try rfl
      rename_i fn
      dsimp only []
      cases fn == targetFn <;> rfl
  have hRRelEq : relocatePC vid targetFn r₀ delta T r = r_rel := by
    unfold r_rel relocatePC
    rw [hLibCaller]
  obtain ⟨hrRelValid, hrRelDepth0⟩ :=
    relocatePC_valid_and_depth vid targetFn r₀ delta T hrValid
  rw [hRRelEq] at hRetRel hrRelValid hrRelDepth0
  have hrCondOrig : (r.valid && decide (r.depth + 1 = pi.depth)) = true := by
    simp [hrValid, hrDepth]
  have hrCondRel : (r_rel.valid && decide (r_rel.depth + 1 = pi.depth)) = true := by
    simp [hrRelValid, hrRelDepth0, hrDepth]
  refine ⟨?_, ?_, ?_, ?_⟩
  · unfold returnRuleProg
    simp only [hd, ite_true, Prog.bind_eq, Prog.rd_bind, exec_read, hRetOrig]
    try dsimp only []
    simp only [hrCondOrig, ite_true, Prog.wr_bind, exec_write]
    rfl
  · unfold returnRuleProg
    simp only [hPiRelDepth, hd, ite_true, Prog.bind_eq, Prog.rd_bind, exec_read, hRetRel]
    try dsimp only []
    simp only [hrCondRel, ite_true, Prog.wr_bind, exec_write]
    rfl
  · unfold returnRuleProg
    simp only [hd, ite_true, Prog.bind_eq, Prog.rd_bind, after_read, hRetOrig]
    try dsimp only []
    simp only [hrCondOrig, ite_true, Prog.wr_bind, after_write]
    simp [Write.apply, Write.affects, Write.newVal, inFrame]
  · unfold returnRuleProg
    simp only [hPiRelDepth, hd, ite_true, Prog.bind_eq, Prog.rd_bind, after_read, hRetRel]
    try dsimp only []
    simp only [hrCondRel, ite_true, Prog.wr_bind, after_write]
    simp [Write.apply, Write.affects, Write.newVal, inFrame]

/-- **T4c.2 `Copy` Coordinate-Relocation Fallthrough (`copy_relocated_step_sim`)**:
at any row `pi.row` of `targetFn` where the insertion point `r₀` is not placed
immediately between `pi.row` and its sequential successor `pi.row + 1`
(`pi.row + 1 ≠ r₀`), executing `Copy` at `relocatePC` advances `kappa` to the
exact relocated successor `relocatePC ... pi.next` and writes the same value. -/
theorem copy_relocated_step_sim {vid : Vid} {targetFn : SymName}
    {r₀ delta : Nat} {T T_rel : Store Key (Val D)} {pi : PC}
    {p q : Key} {val : Val D}
    (hInTarget : T (.frameLib vid pi.depth) = some (.sym targetFn))
    (hNoSplit : pi.row + 1 ≠ r₀)
    (hpOrig : T p = some val) (hpRel : T_rel p = some val)
    (ι : Nat → I) :
    let pi_rel := relocatePC vid targetFn r₀ delta T pi
    let rd1 : Key × Val D := (.dataKey ["lit_rd"], .ref 0 (.absKey p))
    let wr1 : Key × Val D := (.dataKey ["lit_wr"], .ref 0 (.absKey q))
    (exec (copyRuleProg (I := I) (E := E) vid pi [rd1] [wr1]) ι T).1 =
      .ok (pi.next, []) ∧
    (exec (copyRuleProg (I := I) (E := E) vid pi_rel [rd1] [wr1]) ι T_rel).1 =
      .ok (relocatePC vid targetFn r₀ delta T pi.next, []) ∧
    after (copyRuleProg (I := I) (E := E) vid pi_rel [rd1] [wr1]) ι T_rel q =
      (if q = .pcKey vid then some (.pc (relocatePC vid targetFn r₀ delta T pi.next))
       else some val) := by
  intro pi_rel rd1 wr1
  have hShiftSucc : shiftRow r₀ delta pi.row + 1 = shiftRow r₀ delta (pi.row + 1) := by
    unfold shiftRow
    by_cases h1 : r₀ ≤ pi.row
    · have h2 : r₀ ≤ pi.row + 1 := by omega
      simp [h1, h2]; omega
    · have h2 : ¬ (r₀ ≤ pi.row + 1) := by omega
      simp [h1, h2]
  have hPiRelEq : pi_rel = ⟨pi.depth, shiftRow r₀ delta pi.row⟩ := by
    unfold pi_rel relocatePC
    rw [hInTarget]; simp
  have hNextRelEq : pi_rel.next = relocatePC vid targetFn r₀ delta T pi.next := by
    unfold relocatePC
    simp only [hPiRelEq, PC.next, hInTarget]
    simp [hShiftSucc]
  have hResRdOrig : exec (resolveRead (I := I) (E := E) vid pi rd1) ι T = (some p, []) := rfl
  have hResRdRel : exec (resolveRead (I := I) (E := E) vid pi_rel rd1) ι T_rel = (some p, []) := rfl
  refine ⟨?_, ?_, ?_⟩
  · unfold copyRuleProg
    rw [exec_bind_op, hResRdOrig, after_resolveRead]; try dsimp only []
    have hResWr : exec (resolveWrite (I := I) (E := E) vid pi wr1)
        (shiftInputs (resolveRead (I := I) (E := E) vid pi rd1) ι T) T = (some q, []) := rfl
    rw [exec_bind_op, hResWr, after_resolveWrite]; try dsimp only []
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpOrig]
    try dsimp only []
    simp only [Prog.wr_bind, exec_write]
    rfl
  · unfold copyRuleProg
    rw [exec_bind_op, hResRdRel, after_resolveRead]; try dsimp only []
    have hResWr : exec (resolveWrite (I := I) (E := E) vid pi_rel wr1)
        (shiftInputs (resolveRead (I := I) (E := E) vid pi_rel rd1) ι T_rel) T_rel = (some q, []) := rfl
    rw [exec_bind_op, hResWr, after_resolveWrite]; try dsimp only []
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpRel]
    try dsimp only []
    simp only [Prog.wr_bind, exec_write, hNextRelEq]
    rfl
  · unfold copyRuleProg
    rw [after_bind_op, hResRdRel, after_resolveRead]; try dsimp only []
    have hResWr : exec (resolveWrite (I := I) (E := E) vid pi_rel wr1)
        (shiftInputs (resolveRead (I := I) (E := E) vid pi_rel rd1) ι T_rel) T_rel = (some q, []) := rfl
    rw [after_bind_op, hResWr, after_resolveWrite]; try dsimp only []
    simp only [Prog.bind_eq, Prog.rd_bind, after_read, hpRel]
    try dsimp only []
    simp only [Prog.wr_bind, after_write]
    by_cases hqPc : q = .pcKey vid
    · subst hqPc
      simp [Write.apply, Write.affects, Write.newVal, hNextRelEq]
    · simp [Write.apply, Write.affects, Write.newVal, hqPc]

/-- Concrete 2-frame store illustrating Case C2 (`06-control.tex` line 67):
frame 1 runs `"main"` (which called `"callee"` from row 1, where 1 instruction
was inserted at `r₀ = 2`, moving `"main"`'s `return` from row 2 to row 3), while
frame 2 runs `"callee"` at `(2, 1)` (`return`). -/
def c2Store (migrateRetPc : Bool) : Store Key (Val Unit)
  | .pcKey 0 => some (.pc ⟨2, 1⟩)
  | .statusKey 0 => some (.status .running)
  | .stopKey 0 => none
  | .frameLib 0 1 => some (.sym "main")
  | .frameLib 0 2 => some (.sym "callee")
  | .frameRetPc 0 2 =>
      some (.pc ⟨1, if migrateRetPc then shiftRow 2 1 2 else 2⟩)
  | .libInstr "callee" 1 => some (.instr "return" 0 0)
  | .libInstr "main" 2 => some (.instr "inserted_bad_op" 0 0)
  | .libInstr "main" 3 => some (.instr "return" 0 0)
  | _ => none

/-- Minimal empty runtime for evaluating pure core rules on `c2Store`. -/
def emptyRuntime : Runtime Unit Unit Unit where
  caps _ := false
  native _ := { numInputs := 0, eval := fun _ _ _ => .error "none" }
  delegExt _ := { run := fun _ _ _ _ => pure (.error "none") }
  handoffExt _ := { run := fun _ _ _ _ => pure (.error "none") }

/-- **T4c.2 Concrete Counterexample (`unmigrated_returnpc_diverges`)**:
after inserting 1 instruction at row 2 of `"main"` while `"callee"` is active
in frame 2:
1. With `[2]..returnpc` migrated (`c2Store true`), returning from `"callee"` and
   stepping `"main"` lands at `(1, 3)` and completes cleanly with `sigma = ok`;
2. Without migrating `[2]..returnpc` (`c2Store false`), returning from `"callee"`
   lands at the stale row `(1, 2)` and fails on the second step with `sigma = error`. -/
theorem unmigrated_returnpc_diverges :
    let ι : Nat → Unit := fun _ => ()
    let S1_mig := after (gatedStep emptyRuntime 0) ι (c2Store true)
    let S2_mig := after (gatedStep emptyRuntime 0) ι S1_mig
    let S1_unmig := after (gatedStep emptyRuntime 0) ι (c2Store false)
    let S2_unmig := after (gatedStep emptyRuntime 0) ι S1_unmig
    S1_mig (.pcKey 0) = some (.pc ⟨1, 3⟩) ∧
    S2_mig (.statusKey 0) = some (.status .ok) ∧
    S1_unmig (.pcKey 0) = some (.pc ⟨1, 2⟩) ∧
    S2_unmig (.statusKey 0) = some (.status .error) := by
  decide

end LiveEditSimulation

end Pathlang
