import Pathlang.Control

/-!
# Commit-Point Completeness and Fail-Stop (T5)

1. Thread initialization via `vthreadCreate` (`vthread.create`).
2. **Invariant 2 (`inv:commit`, `CommitComplete`)**: at every commit point
   reached from a `vthread.create`-initialized store (and across any
   `ResumableRun` with arbitrary stops, pauses, failures, lease ops, and
   resumes), the store contains a valid counter at `kappa`, the instruction link
   `..lib`, `..callpc`, and signature-matched parameter pointer bindings of
   every live frame, and a valid parent `..returnpc` (`r.depth + 1 = e`) for
   every live frame above the first.
3. Concrete counterexample (`commitComplete_broken_by_libSig_overwrite`) showing
   that signature-cell immutability (`LibSigSafe`) is necessary when `(P)` alone
   permits writing `/lib/{fn}/[0,0]` of an active function on the call stack.
4. **Proposition 6 (`prop:failstop`, `fail_stop`, `fail_stop_with_extensions`)**:
   from any runnable store passing the stop gate (even after an arbitrary
   malformed edit), either a rule steps, or the step fails to `error` while
   leaving `kappa`, every live frame, `rho`, `lambda`, `/lib`, `/sys`, queue
   entries, and user data unchanged (modifying at most `sigma`, `mu`, and frames
   dead under `kappa`).
-/

namespace Pathlang

section ThreadInitAndComplete

variable {D I E : Type}

/-- Membership in any frame directory or frame-owned system key of `vid`. -/
def isFrameOfVid (vid : Vid) : Key → Bool
  | .frameLib v _ => decide (v = vid)
  | .frameCallPc v _ => decide (v = vid)
  | .frameRetPc v _ => decide (v = vid)
  | .frameDir v _ => decide (v = vid)
  | .frameParam v _ _ _ => decide (v = vid)
  | .frameSlot v _ _ => decide (v = vid)
  | _ => false

/-- Ordered atomic writes binding parameter pointers `[d]/[0, +-j] := ptr target`
starting at index `idx`. -/
def paramPtrWrites (vid : Vid) (d : Nat) (dir : ParamDir) :
    Nat → List Key → List (Write Key (Val D))
  | _, [] => []
  | idx, k :: ks =>
      Write.put (.frameParam vid d dir idx) (.ptr k) ::
        paramPtrWrites vid d dir (idx + 1) ks

/-- Store writes performed by `vthread.create` to initialize thread `vid` at
entry function `entryFn` with read/write argument targets `rdTargets` and
`wrTargets`. -/
def vthreadCreateWrites (vid : Vid) (entryFn : SymName)
    (rdTargets wrTargets : List Key) : List (Write Key (Val D)) :=
  [ Write.delWhere (isFrameOfVid vid)
  , Write.del (.stopKey vid)
  , Write.del (.driverKey vid)
  , Write.del (.msgKey vid)
  , Write.put (.frameLib vid 1) (.sym entryFn)
  , Write.put (.frameCallPc vid 1) (.pc ⟨1, 1⟩)
  ] ++
  paramPtrWrites vid 1 .rd 1 rdTargets ++
  paramPtrWrites vid 1 .wr 1 wrTargets ++
  [ Write.put (.pcKey vid) (.pc ⟨1, 1⟩)
  , Write.put (.statusKey vid) (.status .init)
  ]

/-- Store built by `vthread.create` on top of base store `T_base`. -/
def vthreadCreate (vid : Vid) (entryFn : SymName)
    (rdTargets wrTargets : List Key) (T_base : Store Key (Val D)) :
    Store Key (Val D) :=
  applyAll T_base (vthreadCreateWrites vid entryFn rdTargets wrTargets)

/-- **Invariant 2 (`inv:commit`, Commit-Point Completeness)**:
`T` holds a valid counter `pi` at `kappa`, every live frame `1 ≤ e ≤ pi.depth`
holds its `..lib` function link `fn`, `..callpc`, and pointer bindings at all
read/write parameter slots declared by `/lib/{fn}/[0,0]`, and every live frame
`2 ≤ e ≤ pi.depth` holds a valid parent `..returnpc` (`r.depth + 1 = e`). -/
def CommitComplete (vid : Vid) (T : Store Key (Val D)) : Prop :=
  ∃ pi,
    T (.pcKey vid) = some (.pc pi) ∧
    pi.valid = true ∧
    (∀ e, 1 ≤ e → e ≤ pi.depth →
      (∃ fn nr nw,
        T (.frameLib vid e) = some (.sym fn) ∧
        T (.libSig fn) = some (.sig nr nw) ∧
        (∀ j, 1 ≤ j → j ≤ nr → ∃ p, T (.frameParam vid e .rd j) = some (.ptr p)) ∧
        (∀ j, 1 ≤ j → j ≤ nw → ∃ q, T (.frameParam vid e .wr j) = some (.ptr q))) ∧
      (∃ c, T (.frameCallPc vid e) = some (.pc c) ∧ c.valid = true)) ∧
    (∀ e, 2 ≤ e → e ≤ pi.depth →
      ∃ r, T (.frameRetPc vid e) = some (.pc r) ∧ r.valid = true ∧ r.depth + 1 = e)

/-! ### Properties of `paramPtrWrites` and `vthreadCreate` -/

private theorem touches_paramPtrWrites_false {vid : Vid} {d : Nat} {dir : ParamDir}
    {start : Nat} {ks : List Key} {k : Key}
    (hk : ∀ j, start ≤ j → j < start + ks.length → k ≠ .frameParam vid d dir j) :
    touches (paramPtrWrites (D := D) vid d dir start ks) k = false := by
  induction ks generalizing start with
  | nil => rfl
  | cons p ks ih =>
    simp only [paramPtrWrites, touches_cons, Bool.or_eq_false_iff,
      Write.affects, decide_eq_false_iff_not]
    refine ⟨?_, ih (fun j h1 h2 => hk j (by omega) (by simp only [List.length_cons]; omega))⟩
    intro heq
    exact hk start (Nat.le_refl _) (by simp only [List.length_cons]; omega) heq

private theorem applyAll_paramPtrWrites_get {vid : Vid} {d : Nat} {dir : ParamDir}
    {start : Nat} {ks : List Key} (S : Store Key (Val D)) {j : Nat}
    (hlo : start ≤ j) (hhi : j < start + ks.length) :
    ∃ p, applyAll S (paramPtrWrites vid d dir start ks) (.frameParam vid d dir j) =
      some (.ptr p) := by
  induction ks generalizing start S with
  | nil => simp at hhi; omega
  | cons p ks ih =>
    simp only [paramPtrWrites, applyAll_cons]
    rcases Nat.eq_or_lt_of_le hlo with rfl | hgt
    · refine ⟨p, ?_⟩
      have hnt : touches (paramPtrWrites (D := D) vid d dir (start + 1) ks)
          (.frameParam vid d dir start) = false := by
        apply touches_paramPtrWrites_false
        intro j' hj' _ heq
        cases heq
        omega
      rw [applyAll_of_not_touches hnt]
      simp [Write.apply, Write.affects, Write.newVal]
    · exact ih _ (by omega) (by simp only [List.length_cons] at hhi; omega)

theorem vthreadCreate_pc (vid : Vid) (entryFn : SymName)
    (rdTargets wrTargets : List Key) (T_base : Store Key (Val D)) :
    vthreadCreate vid entryFn rdTargets wrTargets T_base (.pcKey vid) =
      some (.pc ⟨1, 1⟩) := by
  unfold vthreadCreate vthreadCreateWrites
  simp [applyAll_append, Write.apply, Write.affects, Write.newVal]

theorem vthreadCreate_status (vid : Vid) (entryFn : SymName)
    (rdTargets wrTargets : List Key) (T_base : Store Key (Val D)) :
    vthreadCreate vid entryFn rdTargets wrTargets T_base (.statusKey vid) =
      some (.status .init) := by
  unfold vthreadCreate vthreadCreateWrites
  simp [applyAll_append, Write.apply, Write.affects, Write.newVal]

theorem vthreadCreate_stop (vid : Vid) (entryFn : SymName)
    (rdTargets wrTargets : List Key) (T_base : Store Key (Val D)) :
    vthreadCreate vid entryFn rdTargets wrTargets T_base (.stopKey vid) = none := by
  unfold vthreadCreate vthreadCreateWrites
  rw [applyAll_append, applyAll_append, applyAll_append]
  have h1 : touches [Write.put (K := Key) (V := Val D) (.pcKey vid) (.pc ⟨1, 1⟩),
      Write.put (.statusKey vid) (.status .init)] (.stopKey vid) = false := rfl
  have h2 : touches (paramPtrWrites (D := D) vid 1 .wr 1 wrTargets) (.stopKey vid) = false :=
    touches_paramPtrWrites_false (fun _ _ _ h => by cases h)
  have h3 : touches (paramPtrWrites (D := D) vid 1 .rd 1 rdTargets) (.stopKey vid) = false :=
    touches_paramPtrWrites_false (fun _ _ _ h => by cases h)
  rw [applyAll_of_not_touches h1, applyAll_of_not_touches h2, applyAll_of_not_touches h3]
  simp [Write.apply, Write.affects, Write.newVal]

theorem vthreadCreate_libSig (vid : Vid) (entryFn fn : SymName)
    (rdTargets wrTargets : List Key) (T_base : Store Key (Val D)) :
    vthreadCreate vid entryFn rdTargets wrTargets T_base (.libSig fn) =
      T_base (.libSig fn) := by
  unfold vthreadCreate vthreadCreateWrites
  rw [applyAll_append, applyAll_append, applyAll_append]
  have h1 : touches [Write.put (K := Key) (V := Val D) (.pcKey vid) (.pc ⟨1, 1⟩),
      Write.put (.statusKey vid) (.status .init)] (.libSig fn) = false := rfl
  have h2 : touches (paramPtrWrites (D := D) vid 1 .wr 1 wrTargets) (.libSig fn) = false :=
    touches_paramPtrWrites_false (fun _ _ _ h => by cases h)
  have h3 : touches (paramPtrWrites (D := D) vid 1 .rd 1 rdTargets) (.libSig fn) = false :=
    touches_paramPtrWrites_false (fun _ _ _ h => by cases h)
  rw [applyAll_of_not_touches h1, applyAll_of_not_touches h2, applyAll_of_not_touches h3]
  simp [Write.apply, Write.affects, isFrameOfVid]

/-- `vthreadCreate` produces a runnable `ValidCommitPoint`. -/
theorem vthreadCreate_validCommitPoint (vid : Vid) (entryFn : SymName)
    (rdTargets wrTargets : List Key) (T_base : Store Key (Val D)) :
    ValidCommitPoint vid (vthreadCreate vid entryFn rdTargets wrTargets T_base) := by
  refine Or.inl ⟨⟨.init, vthreadCreate_status vid entryFn rdTargets wrTargets T_base, rfl⟩,
    vthreadCreate_stop vid entryFn rdTargets wrTargets T_base⟩

/-- **Invariant 2 Base Case (`vthreadCreate_commitComplete`)**:
if `entryFn` has signature `(rdTargets.length, wrTargets.length)` in `T_base`,
then the store constructed by `vthreadCreate` satisfies `CommitComplete`. -/
theorem vthreadCreate_commitComplete (vid : Vid) (entryFn : SymName)
    (rdTargets wrTargets : List Key) (T_base : Store Key (Val D))
    (hSig : T_base (.libSig entryFn) = some (.sig rdTargets.length wrTargets.length)) :
    CommitComplete vid (vthreadCreate vid entryFn rdTargets wrTargets T_base) := by
  refine ⟨⟨1, 1⟩, vthreadCreate_pc vid entryFn rdTargets wrTargets T_base, rfl, ?_, ?_⟩
  · intro e hlo hhi
    dsimp only [] at hhi
    have he1 : e = 1 := by omega
    subst he1
    have hTail : ∀ k, k ≠ .pcKey vid → k ≠ .statusKey vid →
        vthreadCreate vid entryFn rdTargets wrTargets T_base k = applyAll
          (applyAll
            (applyAll T_base
              [ Write.delWhere (isFrameOfVid vid)
              , Write.del (.stopKey vid)
              , Write.del (.driverKey vid)
              , Write.del (.msgKey vid)
              , Write.put (.frameLib vid 1) (.sym entryFn)
              , Write.put (.frameCallPc vid 1) (.pc ⟨1, 1⟩) ])
            (paramPtrWrites vid 1 .rd 1 rdTargets))
          (paramPtrWrites vid 1 .wr 1 wrTargets) k := by
      intro k hk1 hk2
      unfold vthreadCreate vthreadCreateWrites
      have hnt : touches [Write.put (K := Key) (V := Val D) (.pcKey vid) (.pc ⟨1, 1⟩),
          Write.put (.statusKey vid) (.status .init)] k = false := by
        simp [Write.affects, hk1, hk2]
      rw [applyAll_append, applyAll_of_not_touches hnt, applyAll_append, applyAll_append]
    refine ⟨⟨entryFn, rdTargets.length, wrTargets.length, ?_, ?_, ?_, ?_⟩,
      ⟨⟨1, 1⟩, ?_, rfl⟩⟩
    · rw [hTail _ (by intro h; cases h) (by intro h; cases h)]
      rw [applyAll_of_not_touches (touches_paramPtrWrites_false (fun _ _ _ h => by cases h))]
      rw [applyAll_of_not_touches (touches_paramPtrWrites_false (fun _ _ _ h => by cases h))]
      simp [Write.apply, Write.affects, Write.newVal]
    · rw [vthreadCreate_libSig]
      exact hSig
    · intro j hj1 hj2
      rw [hTail _ (by intro h; cases h) (by intro h; cases h)]
      rw [applyAll_of_not_touches (touches_paramPtrWrites_false (fun _ _ _ h => by cases h))]
      exact applyAll_paramPtrWrites_get _ hj1 (by omega)
    · intro j hj1 hj2
      rw [hTail _ (by intro h; cases h) (by intro h; cases h)]
      exact applyAll_paramPtrWrites_get _ hj1 (by omega)
    · rw [hTail _ (by intro h; cases h) (by intro h; cases h)]
      rw [applyAll_of_not_touches (touches_paramPtrWrites_false (fun _ _ _ h => by cases h))]
      rw [applyAll_of_not_touches (touches_paramPtrWrites_false (fun _ _ _ h => by cases h))]
      simp [Write.apply, Write.affects, Write.newVal]
  · intro e hlo hhi
    dsimp only [] at hhi
    omega

/-! ### Invariance of `CommitComplete` under `StoreAgree` (`~`) -/

private theorem live_frameLib_not_ignored {vid : Vid} {d e : Nat}
    (h1 : 1 ≤ e) (h2 : e ≤ d) :
    ¬ IgnoredByAgree vid d (.frameLib vid e) := by
  rintro (h | h | h | h | hdead) <;> try cases h
  simp only [isDeadFrameKey, decide_eq_true_eq] at hdead
  omega

private theorem live_frameCallPc_not_ignored {vid : Vid} {d e : Nat}
    (h1 : 1 ≤ e) (h2 : e ≤ d) :
    ¬ IgnoredByAgree vid d (.frameCallPc vid e) := by
  rintro (h | h | h | h | hdead) <;> try cases h
  simp only [isDeadFrameKey, decide_eq_true_eq] at hdead
  omega

private theorem live_frameRetPc_not_ignored {vid : Vid} {d e : Nat}
    (h1 : 1 ≤ e) (h2 : e ≤ d) :
    ¬ IgnoredByAgree vid d (.frameRetPc vid e) := by
  rintro (h | h | h | h | hdead) <;> try cases h
  simp only [isDeadFrameKey, decide_eq_true_eq] at hdead
  omega

private theorem live_frameParam_not_ignored {vid : Vid} {d e : Nat}
    (dir : ParamDir) (j : Nat) (h1 : 1 ≤ e) (h2 : e ≤ d) :
    ¬ IgnoredByAgree vid d (.frameParam vid e dir j) := by
  rintro (h | h | h | h | hdead) <;> try cases h
  simp only [isDeadFrameKey, decide_eq_true_eq] at hdead
  omega

private theorem libSig_not_ignored (vid : Vid) (d : Nat) (fn : SymName) :
    ¬ IgnoredByAgree vid d (.libSig fn) := by
  rintro (h | h | h | h | h) <;> cases h

/-- `CommitComplete` is invariant under `StoreAgree` (`~`), since `StoreAgree`
preserves `kappa`, every live frame `1 ≤ e ≤ pi.depth`, and `/lib`. -/
theorem CommitComplete.of_storeAgree {vid : Vid} {T U : Store Key (Val D)}
    (hC : CommitComplete vid T) (hAg : StoreAgree vid T U) :
    CommitComplete vid U := by
  obtain ⟨pi, hpcT, hpv, hFrames, hRet⟩ := hC
  have hpcU : U (.pcKey vid) = some (.pc pi) := hAg.1.symm.trans hpcT
  have hd : liveDepthOf vid T = pi.depth := liveDepthOf_pc hpcT
  refine ⟨pi, hpcU, hpv, ?_, ?_⟩
  · intro e h1 h2
    obtain ⟨⟨fn, nr, nw, hLib, hSig, hRd, hWr⟩, ⟨c, hCall, hcv⟩⟩ := hFrames e h1 h2
    refine ⟨⟨fn, nr, nw, ?_, ?_, ?_, ?_⟩, ⟨c, ?_, hcv⟩⟩
    · rw [← hAg.2 _ (by rw [hd]; exact live_frameLib_not_ignored h1 h2)]; exact hLib
    · rw [← hAg.2 _ (libSig_not_ignored vid _ fn)]; exact hSig
    · intro j hj1 hj2
      rw [← hAg.2 _ (by rw [hd]; exact live_frameParam_not_ignored .rd j h1 h2)]
      exact hRd j hj1 hj2
    · intro j hj1 hj2
      rw [← hAg.2 _ (by rw [hd]; exact live_frameParam_not_ignored .wr j h1 h2)]
      exact hWr j hj1 hj2
    · rw [← hAg.2 _ (by rw [hd]; exact live_frameCallPc_not_ignored h1 h2)]; exact hCall
  · intro e h2 hle
    obtain ⟨r, hr, hrv, hrd⟩ := hRet e h2 hle
    refine ⟨r, ?_, hrv, hrd⟩
    rw [← hAg.2 _ (by rw [hd]; exact live_frameRetPc_not_ignored (by omega) hle)]
    exact hr

end ThreadInitAndComplete

/-! ## Single-Step and Run-Wide Preservation of Invariant 2 (`inv:commit`) -/

section CommitPreservation

variable {D I E : Type}

/-- Signature-cell immutability during a step: no write of `stepCore` modifies
any `/lib/{fn}/[0,0]` (`Key.libSig fn`). Holds unconditionally for `Goto`, `Br`,
`Call`, `Return`, and `UserCall`, and holds for `Copy`, `Native`, `Deleg`, and
`Handoff` whenever write operands and extensions do not overwrite `/lib/*/[0,0]`. -/
def LibSigSafe (R : Runtime D I E) (vid : Vid) (ι : Nat → I)
    (T : Store Key (Val D)) : Prop :=
  ∀ fn, touches (Act.writes (exec (stepCore R vid) ι T).2) (.libSig fn) = false

theorem LibSigSafe.of_storeAgree {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T U : Store Key (Val D)}
    (hAg : StoreAgree vid T U) (hP : StepDiscipline vid T) (ι : Nat → I)
    (hSafe : LibSigSafe R vid ι T) :
    LibSigSafe R vid ι U := by
  intro fn
  rw [stepCore_exec_congr_agree (E := E) hR hAg hP ι]
  exact hSafe fn

/-- Syntactic store condition ensuring no write operand at `T`'s current
instruction resolves to `/lib/{fn}/[0,0]`. -/
def WriteOpsAvoidLibSig (vid : Vid) (T : Store Key (Val D)) : Prop :=
  ∀ pi dec,
    T (.pcKey vid) = some (.pc pi) →
    decodeInstrPure vid pi T = .ok dec →
    ∀ y ∈ dec.wrOps, ∀ q, resolveWritePure vid pi y T = some q →
      ∀ fn, q ≠ .libSig fn

/-- Extension discipline ensuring extension writes do not overwrite `/lib/{fn}/[0,0]`. -/
def ExtAvoidsLibSig (ext : Extension D I E) : Prop :=
  ∀ vid pi rdOps wrOps ι T out k v,
    (exec (ext.run vid pi rdOps wrOps) ι T).1 = .ok out →
    (k, v) ∈ out.writes →
    ∀ fn, k ≠ .libSig fn

/-- Preservation of live-frame metadata and parameter pointers up to depth `dKeep`. -/
def PreservesFramesUpTo (vid : Vid) (dKeep : Nat)
    (T T' : Store Key (Val D)) : Prop :=
  (∀ e, 1 ≤ e → e ≤ dKeep →
    T' (.frameLib vid e) = T (.frameLib vid e) ∧
    T' (.frameCallPc vid e) = T (.frameCallPc vid e) ∧
    (∀ dir j, T' (.frameParam vid e dir j) = T (.frameParam vid e dir j))) ∧
  (∀ e, 2 ≤ e → e ≤ dKeep →
    T' (.frameRetPc vid e) = T (.frameRetPc vid e))

/-- Helper: if `T'` updates `kappa` to a valid `pi'` with `pi'.depth ≤ pi.depth`,
preserves frames up to `pi'.depth`, and preserves `/lib/*/[0,0]`, then
`CommitComplete` carries from `T` to `T'`. -/
private theorem commitComplete_of_le_depth {vid : Vid} {T T' : Store Key (Val D)}
    {pi pi' : PC}
    (hC : CommitComplete vid T)
    (hpcT : T (.pcKey vid) = some (.pc pi))
    (hpcT' : T' (.pcKey vid) = some (.pc pi'))
    (hpv' : pi'.valid = true)
    (hle : pi'.depth ≤ pi.depth)
    (hPres : PreservesFramesUpTo vid pi'.depth T T')
    (hSigEq : ∀ fn, T' (.libSig fn) = T (.libSig fn)) :
    CommitComplete vid T' := by
  obtain ⟨pi₀, hpc₀, _, hFrames, hRet⟩ := hC
  rw [hpcT] at hpc₀
  cases hpc₀
  refine ⟨pi', hpcT', hpv', ?_, ?_⟩
  · intro e h1 h2
    obtain ⟨⟨fn, nr, nw, hLib, hSig, hRd, hWr⟩, ⟨c, hCall, hcv⟩⟩ :=
      hFrames e h1 (Nat.le_trans h2 hle)
    obtain ⟨hLibEq, hCallEq, hParamEq⟩ := hPres.1 e h1 h2
    refine ⟨⟨fn, nr, nw, hLibEq.trans hLib, (hSigEq fn).trans hSig, ?_, ?_⟩,
      ⟨c, hCallEq.trans hCall, hcv⟩⟩
    · intro j hj1 hj2; rw [hParamEq]; exact hRd j hj1 hj2
    · intro j hj1 hj2; rw [hParamEq]; exact hWr j hj1 hj2
  · intro e h2 hle'
    obtain ⟨r, hr, hrv, hrd⟩ := hRet e h2 (Nat.le_trans hle' hle)
    exact ⟨r, (hPres.2 e h2 hle').trans hr, hrv, hrd⟩

/-- Predicate identifying the frame metadata and parameter binding keys of `vid`
whose preservation Invariant 2 requires. -/
def isFrameCompleteKey (vid : Vid) : Key → Bool
  | .frameLib v _ => decide (v = vid)
  | .frameCallPc v _ => decide (v = vid)
  | .frameRetPc v _ => decide (v = vid)
  | .frameParam v _ _ _ => decide (v = vid)
  | _ => false

private theorem not_frameCompleteKey_of_safe {vid : Vid} {d : Nat} {k : Key}
    (hF : ForbiddenKey vid d k = false) (hP : isFrameParamKey vid k = false) :
    isFrameCompleteKey vid k = false := by
  cases k <;>
    simp only [ForbiddenKey, isThreadControlKey, isFrameParamKey, isFrameCompleteKey,
      Bool.or_eq_false_iff, decide_eq_false_iff_not] at hF hP ⊢
  · exact hF.2
  · exact hF.2
  · exact hF.2
  · exact hP

private theorem putAll_preserves_frameCompleteKey {vid : Vid} {d : Nat}
    {kvs : List (Key × Val D)}
    (hkvs : ∀ k v, (k, v) ∈ kvs → ForbiddenKey vid d k = false ∧ isFrameParamKey vid k = false)
    (ι : Nat → I) (S : Store Key (Val D)) {k : Key}
    (hk : isFrameCompleteKey vid k = true) :
    after (putAll (I := I) (E := E) kvs) ι S k = S k := by
  induction kvs generalizing ι S with
  | nil => rfl
  | cons kv kvs ih =>
    rcases kv with ⟨k', v'⟩
    unfold putAll
    simp only [Prog.bind_eq, Prog.wr_bind, after_write]
    have hk'False : isFrameCompleteKey vid k' = false :=
      not_frameCompleteKey_of_safe (hkvs k' v' List.mem_cons_self).1
        (hkvs k' v' List.mem_cons_self).2
    have hne : k ≠ k' := by
      rintro rfl; rw [hk] at hk'False; cases hk'False
    rw [ih (fun a b hm => hkvs a b (List.mem_cons_of_mem _ hm)) ι _]
    simp [Write.apply, Write.affects, hne]

private theorem preservesFrames_of_eq_frameCompleteKey {vid : Vid} (dKeep : Nat)
    {T T' : Store Key (Val D)}
    (hEq : ∀ k, isFrameCompleteKey vid k = true → T' k = T k) :
    PreservesFramesUpTo vid dKeep T T' := by
  refine ⟨?_, ?_⟩
  · intro e _ _
    refine ⟨hEq _ (by simp [isFrameCompleteKey]),
      hEq _ (by simp [isFrameCompleteKey]),
      fun dir j => hEq _ (by simp [isFrameCompleteKey])⟩
  · intro e _ _
    exact hEq _ (by simp [isFrameCompleteKey])

/-! ### Exact Post-State Characterization of `bindReadArgs`, `bindWriteArgs`, and `bindChild` -/

theorem bindReadArgs_writes_child (vid : Vid) (pi : PC) (idx : Nat)
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

theorem bindWriteArgs_writes_child (vid : Vid) (pi : PC) (idx : Nat)
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

/-- Every write of `bindChild` (whether succeeding or failing) affects only
`inFrame vid (pi.depth + 1)`. -/
theorem bindChild_only_writes_child (vid : Vid) (pi : PC) (callee : SymName)
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

private theorem bindReadArgs_ok_post {vid : Vid} {pi : PC} {start : Nat}
    {xs : List (Key × Val D)} {ι : Nat → I} {S : Store Key (Val D)}
    (hok : (exec (bindReadArgs (I := I) (E := E) vid pi start xs) ι S).1 = .ok ()) :
    (∀ k, (∀ j, start ≤ j → j < start + xs.length →
        k ≠ .frameParam vid (pi.depth + 1) .rd j) →
      after (bindReadArgs (I := I) (E := E) vid pi start xs) ι S k = S k) ∧
    (∀ j, start ≤ j → j < start + xs.length →
      ∃ p, after (bindReadArgs (I := I) (E := E) vid pi start xs) ι S
        (.frameParam vid (pi.depth + 1) .rd j) = some (.ptr p)) := by
  induction xs generalizing start ι S with
  | nil =>
    refine ⟨fun _ _ => rfl, ?_⟩
    intro j h1 h2; simp at h2; omega
  | cons x xs ih =>
    unfold bindReadArgs at hok ⊢
    rw [exec_bind_fst, after_resolveRead] at hok
    rw [after_bind_op, after_resolveRead]
    cases hp : (exec (resolveRead (I := I) (E := E) vid pi x) ι S).1 with
    | none => rw [hp] at hok; simp at hok
    | some p =>
      rw [hp] at hok
      dsimp only [] at hok ⊢
      simp only [Prog.bind_eq, Prog.wr_bind, exec_write, after_write] at hok ⊢
      obtain ⟨hUnch, hBound⟩ := ih hok
      refine ⟨?_, ?_⟩
      · intro k hk
        rw [hUnch k (fun j hj1 hj2 => hk j (by omega) (by simp only [List.length_cons]; omega))]
        have hne : k ≠ .frameParam vid (pi.depth + 1) .rd start :=
          hk start (Nat.le_refl _) (by simp only [List.length_cons]; omega)
        simp [Write.apply, Write.affects, hne]
      · intro j hj1 hj2
        rcases Nat.eq_or_lt_of_le hj1 with rfl | hgt
        · refine ⟨p, ?_⟩
          rw [hUnch _ (fun j' hj' _ heq => by cases heq; omega)]
          simp [Write.apply, Write.affects, Write.newVal]
        · exact hBound j (by omega) (by simp only [List.length_cons] at hj2; omega)

private theorem bindWriteArgs_ok_post {vid : Vid} {pi : PC} {start : Nat}
    {ys : List (Key × Val D)} {ι : Nat → I} {S : Store Key (Val D)}
    (hok : (exec (bindWriteArgs (I := I) (E := E) vid pi start ys) ι S).1 = .ok ()) :
    (∀ k, (∀ j, start ≤ j → j < start + ys.length →
        k ≠ .frameParam vid (pi.depth + 1) .wr j) →
      after (bindWriteArgs (I := I) (E := E) vid pi start ys) ι S k = S k) ∧
    (∀ j, start ≤ j → j < start + ys.length →
      ∃ q, after (bindWriteArgs (I := I) (E := E) vid pi start ys) ι S
        (.frameParam vid (pi.depth + 1) .wr j) = some (.ptr q)) := by
  induction ys generalizing start ι S with
  | nil =>
    refine ⟨fun _ _ => rfl, ?_⟩
    intro j h1 h2; simp at h2; omega
  | cons y ys ih =>
    unfold bindWriteArgs at hok ⊢
    rw [exec_bind_fst, after_resolveWrite] at hok
    rw [after_bind_op, after_resolveWrite]
    cases hq : (exec (resolveWrite (I := I) (E := E) vid pi y) ι S).1 with
    | none => rw [hq] at hok; simp at hok
    | some q =>
      rw [hq] at hok
      dsimp only [] at hok ⊢
      simp only [Prog.bind_eq, Prog.wr_bind, exec_write, after_write] at hok ⊢
      obtain ⟨hUnch, hBound⟩ := ih hok
      refine ⟨?_, ?_⟩
      · intro k hk
        rw [hUnch k (fun j hj1 hj2 => hk j (by omega) (by simp only [List.length_cons]; omega))]
        have hne : k ≠ .frameParam vid (pi.depth + 1) .wr start :=
          hk start (Nat.le_refl _) (by simp only [List.length_cons]; omega)
        simp [Write.apply, Write.affects, hne]
      · intro j hj1 hj2
        rcases Nat.eq_or_lt_of_le hj1 with rfl | hgt
        · refine ⟨q, ?_⟩
          rw [hUnch _ (fun j' hj' _ heq => by cases heq; omega)]
          simp [Write.apply, Write.affects, Write.newVal]
        · exact hBound j (by omega) (by simp only [List.length_cons] at hj2; omega)

private theorem commitComplete_after_bindChild_and_entry {vid : Vid} {pi : PC}
    {callee : SymName} {rdArgs wrArgs : List (Key × Val D)}
    {ι : Nat → I} {T : Store Key (Val D)}
    (hC : CommitComplete vid T)
    (hpc : T (.pcKey vid) = some (.pc pi))
    (hpv : pi.valid = true)
    (hb : (exec (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T).1 = .ok ()) :
    CommitComplete vid
      ((Write.put (.pcKey vid) (.pc pi.entry)).apply
        (after (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T)) := by
  let T_b := after (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T
  let T' := (Write.put (.pcKey vid) (.pc pi.entry)).apply T_b
  have hav := bindChild_only_writes_child (I := I) (E := E) vid pi callee rdArgs wrArgs ι T
  obtain ⟨pi₀, hpc₀, _, hFramesT, hRetT⟩ := hC
  rw [hpc] at hpc₀
  cases hpc₀
  -- Unpack `bindChild` success
  have hb' := hb
  unfold bindChild at hb'
  rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hb'
  cases hvSig : T (.libSig callee) with
  | none => rw [hvSig] at hb'; simp at hb'
  | some vSig =>
    rw [hvSig] at hb'
    cases vSig <;> try (simp at hb'; done)
    rename_i sigNr sigNw
    dsimp only [] at hb'
    cases har : (sigNr != rdArgs.length || sigNw != wrArgs.length) with
    | true => rw [har] at hb'; simp at hb'
    | false =>
      rw [har] at hb'
      simp only [Bool.false_eq_true, ite_false] at hb'
      have hArith : sigNr = rdArgs.length ∧ sigNw = wrArgs.length := by
        simpa [Bool.or_eq_false_iff, bne_eq_false_iff_eq] using har
      rcases hArith with ⟨rfl, rfl⟩
      simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hb'
      rw [← Prog.bind_eq, exec_bind_fst] at hb'
      let dChild := pi.depth + 1
      let S₁ := (Write.put (.frameCallPc vid dChild) (.pc pi.entry)).apply
        ((Write.put (.frameRetPc vid dChild) (.pc pi.next)).apply
          ((Write.put (.frameLib vid dChild) (.sym callee)).apply
            ((Write.delWhere (inFrame vid dChild)).apply T)))
      cases hr1 : (exec (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) ι S₁).1 with
      | error _ => rw [hr1] at hb'; simp at hb'
      | ok u1 =>
        cases u1
        rw [hr1] at hb'
        dsimp only [] at hb'
        let S₂ := after (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) ι S₁
        let ι₂ := shiftInputs (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) ι S₁
        have hTbEq : T_b = after (bindWriteArgs (I := I) (E := E) vid pi 1 wrArgs) ι₂ S₂ := by
          show after (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T = _
          unfold bindChild
          simp only [Prog.bind_eq, Prog.rd_bind, after_read, hvSig]
          try dsimp only []
          simp only [har, Bool.false_eq_true, ite_false, Prog.wr_bind, after_write]
          rw [← Prog.bind_eq, after_bind_op, hr1]
        obtain ⟨hRdUnch, hRdBound⟩ := bindReadArgs_ok_post hr1
        obtain ⟨hWrUnch, hWrBound⟩ := bindWriteArgs_ok_post hb'
        have hT'NonPc : ∀ k, k ≠ .pcKey vid →
            (Write.put (.pcKey vid) (.pc pi.entry)).apply T_b k = T_b k := by
          intro k hne; simp [Write.apply, Write.affects, hne]
        have hTbCaller : ∀ k, inFrame vid dChild k = false → T_b k = T k :=
          fun k hnf => hav.after_eq hnf
        change CommitComplete vid ((Write.put (.pcKey vid) (.pc pi.entry)).apply T_b)
        refine ⟨pi.entry, ?_, PC.entry_valid pi, ?_, ?_⟩
        · simp [Write.apply, Write.affects, Write.newVal]
        · intro e h1 hle
          simp only [PC.entry_depth] at hle
          rcases Nat.lt_or_eq_of_le hle with hlt | rfl
          · have hleP : e ≤ pi.depth := by omega
            have hneChild : e ≠ dChild := by omega
            obtain ⟨⟨fn, nr, nw, hLib, hSig, hRd, hWr⟩, ⟨c, hCall, hcv⟩⟩ := hFramesT e h1 hleP
            refine ⟨⟨fn, nr, nw, ?_, ?_, ?_, ?_⟩, ⟨c, ?_, hcv⟩⟩
            · rw [hT'NonPc _ (by intro h; cases h),
                  hTbCaller _ (not_inFrame_of_ne hneChild (by simp [inFrame]))]
              exact hLib
            · rw [hT'NonPc _ (by intro h; cases h), hTbCaller _ rfl]
              exact hSig
            · intro j hj1 hj2
              rw [hT'NonPc _ (by intro h; cases h),
                  hTbCaller _ (not_inFrame_of_ne hneChild (by simp [inFrame]))]
              exact hRd j hj1 hj2
            · intro j hj1 hj2
              rw [hT'NonPc _ (by intro h; cases h),
                  hTbCaller _ (not_inFrame_of_ne hneChild (by simp [inFrame]))]
              exact hWr j hj1 hj2
            · rw [hT'NonPc _ (by intro h; cases h),
                  hTbCaller _ (not_inFrame_of_ne hneChild (by simp [inFrame]))]
              exact hCall
          · -- New live child frame `e = dChild = pi.depth + 1`
            refine ⟨⟨callee, rdArgs.length, wrArgs.length, ?_, ?_, ?_, ?_⟩,
              ⟨pi.entry, ?_, PC.entry_valid pi⟩⟩
            · rw [hT'NonPc _ (by intro h; cases h), hTbEq,
                  hWrUnch _ (fun _ _ _ h => by cases h),
                  hRdUnch _ (fun _ _ _ h => by cases h)]
              simp [S₁, dChild, Write.apply, Write.affects, Write.newVal]
            · rw [hT'NonPc _ (by intro h; cases h), hTbCaller _ rfl]
              exact hvSig
            · intro j hj1 hj2
              rw [hT'NonPc _ (by intro h; cases h), hTbEq,
                  hWrUnch _ (fun _ _ _ h => by cases h)]
              exact hRdBound j hj1 (by omega)
            · intro j hj1 hj2
              rw [hT'NonPc _ (by intro h; cases h), hTbEq]
              exact hWrBound j hj1 (by omega)
            · rw [hT'NonPc _ (by intro h; cases h), hTbEq,
                  hWrUnch _ (fun _ _ _ h => by cases h),
                  hRdUnch _ (fun _ _ _ h => by cases h)]
              simp [S₁, dChild, Write.apply, Write.affects, Write.newVal]
        · intro e h2 hle
          simp only [PC.entry_depth] at hle
          rcases Nat.lt_or_eq_of_le hle with hlt | rfl
          · have hleP : e ≤ pi.depth := by omega
            have hneChild : e ≠ dChild := by omega
            obtain ⟨r, hr, hrv, hrd⟩ := hRetT e h2 hleP
            refine ⟨r, ?_, hrv, hrd⟩
            rw [hT'NonPc _ (by intro h; cases h),
                hTbCaller _ (not_inFrame_of_ne hneChild (by simp [inFrame]))]
            exact hr
          · refine ⟨pi.next, ?_, PC.next_valid hpv, rfl⟩
            rw [hT'NonPc _ (by intro h; cases h), hTbEq,
                hWrUnch _ (fun _ _ _ h => by cases h),
                hRdUnch _ (fun _ _ _ h => by cases h)]
            simp [S₁, dChild, Write.apply, Write.affects, Write.newVal]

private theorem resolveRowTarget_ge_one {vid : Vid} {pi : PC} {x : Key × Val D}
    {ι : Nat → I} {T : Store Key (Val D)} {n : Nat}
    (h : (exec (resolveRowTarget (I := I) (E := E) vid pi x) ι T).1 = some n) :
    1 ≤ n := by
  unfold resolveRowTarget at h
  rw [exec_bind_fst, after_resolveRead] at h
  cases hp : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => rw [hp] at h; simp at h
  | some p =>
    rw [hp] at h; dsimp only [] at h
    simp only [Prog.bind_eq, Prog.rd_bind, exec_read] at h
    cases hv : T p with
    | none => rw [hv] at h; simp at h
    | some v =>
      rw [hv] at h
      cases v <;> try (simp at h; done)
      rename_i z
      cases z with
      | negSucc _ => simp at h
      | ofNat m =>
        cases m with
        | zero => simp at h
        | succ m' => simp at h; omega

private theorem after_resolveRowTarget_same (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveRowTarget (I := I) (E := E) vid pi x) ι T = T := by
  unfold resolveRowTarget
  rw [after_bind_op, after_resolveRead]
  cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => rfl
  | some p =>
    dsimp only []
    simp only [Prog.bind_eq, Prog.rd_bind, after_read]
    cases T p with
    | none => rfl
    | some v =>
      cases v <;> try rfl
      rename_i z
      cases z with
      | negSucc _ => rfl
      | ofNat m => cases m <;> rfl

private theorem after_resolveTruth_same (vid : Vid) (pi : PC) (x : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveTruth (I := I) (E := E) vid pi x) ι T = T := by
  unfold resolveTruth
  rw [after_bind_op, after_resolveRead]
  cases (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => rfl
  | some p =>
    dsimp only []
    simp only [Prog.bind_eq, Prog.rd_bind, after_read]
    cases T p with
    | none => rfl
    | some v => cases v <;> rfl

private theorem after_resolveAllRead_same (vid : Vid) (pi : PC)
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
      dsimp only []
      rw [after_bind_op, ih]
      cases (exec (resolveAllRead (I := I) (E := E) vid pi xs) _ T).1 <;> rfl

private theorem after_resolveAllWrite_same (vid : Vid) (pi : PC)
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
      dsimp only []
      rw [after_bind_op, ih]
      cases (exec (resolveAllWrite (I := I) (E := E) vid pi ys) _ T).1 <;> rfl

private theorem resolveAllWrite_safe_list {vid : Vid} {pi : PC}
    {ys : List (Key × Val D)} {T : Store Key (Val D)}
    (hys : ∀ y ∈ ys, WriteOpSafe vid pi T y) {ι : Nat → I} {qs : List Key}
    (hqs : (exec (resolveAllWrite (I := I) (E := E) vid pi ys) ι T).1 = some qs) :
    ∀ q ∈ qs, ForbiddenKey vid pi.depth q = false ∧ isFrameParamKey vid q = false := by
  induction ys generalizing ι qs with
  | nil =>
    simp [resolveAllWrite] at hqs
    subst hqs; intro _ hm; cases hm
  | cons y ys ih =>
    unfold resolveAllWrite at hqs
    rw [exec_bind_fst, after_resolveWrite] at hqs
    cases hq : (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
    | none => rw [hq] at hqs; simp at hqs
    | some q0 =>
      rw [hq] at hqs; dsimp only [] at hqs
      rw [exec_bind_fst] at hqs
      cases hrest : (exec (resolveAllWrite (I := I) (E := E) vid pi ys) _ T).1 with
      | none => rw [hrest] at hqs; simp at hqs
      | some qsRest =>
        rw [hrest] at hqs; simp at hqs
        subst hqs
        intro q hmem
        rcases List.mem_cons.1 hmem with rfl | hmem'
        · have hqPure := hq; rw [exec_resolveWrite] at hqPure
          exact (hys y List.mem_cons_self).2 q hqPure
        · exact ih (fun z hz => hys z (List.mem_cons_of_mem _ hz)) hrest q hmem'

private theorem after_readKeyList_same (ks : List Key) (ι : Nat → I) (T : Store Key (Val D)) :
    after (readKeyList (D := D) (I := I) (E := E) ks) ι T = T := by
  induction ks generalizing ι with
  | nil => rfl
  | cons k ks ih =>
    unfold readKeyList
    simp only [Prog.bind_eq, Prog.rd_bind, after_read]
    rw [← Prog.bind_eq, after_bind_op, ih]
    rfl

private theorem after_recvMany_same (n : Nat) (ι : Nat → I) (T : Store Key (Val D)) :
    after (recvMany (D := D) (I := I) (E := E) n) ι T = T := by
  induction n generalizing ι with
  | zero => rfl
  | succ n ih =>
    unfold recvMany
    simp only [Prog.bind_eq, Prog.recv_bind, after_input]
    rw [← Prog.bind_eq, after_bind_op, ih]
    rfl

private theorem after_emitAll_same (evs : List E) (ι : Nat → I) (T : Store Key (Val D)) :
    after (emitAll (D := D) (I := I) evs) ι T = T := by
  induction evs generalizing ι with
  | nil => rfl
  | cons _ evs ih =>
    unfold emitAll
    rw [Prog.bind_eq, Prog.out_bind, after_emit]
    exact ih ι

private theorem after_ext_same {ext : Extension D I E} (hExt : ExtensionSpec ext)
    (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D))
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (ext.run vid pi rdOps wrOps) ι T = T := by
  unfold after
  rw [(hExt.internalPure vid pi rdOps wrOps ι T).1]
  rfl

/-- Single successful `stepCore` preserves `CommitComplete` under `RuntimeSpec`,
`(P)`, and `LibSigSafe`. -/
theorem commitComplete_stepCore_ok {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)}
    (hC : CommitComplete vid T)
    (hP : StepDiscipline vid T)
    {ι : Nat → I} (hSigSafe : LibSigSafe R vid ι T)
    {pi' : PC} {evs : List E} {asCore : List (Act Key (Val D) I E)}
    (hOk : exec (stepCore R vid) ι T = (.ok (pi', evs), asCore)) :
    CommitComplete vid (after (stepCore R vid) ι T) := by
  have hSigEq : ∀ fn, after (stepCore R vid) ι T (.libSig fn) = T (.libSig fn) := by
    intro fn
    exact applyAll_of_not_touches (hSigSafe fn)
  have hFst : (exec (stepCore R vid) ι T).1 = .ok (pi', evs) := by rw [hOk]
  clear hOk asCore hSigSafe
  obtain ⟨pi, hpc, hpv, _, _⟩ := id hC
  have hFst0 := hFst
  unfold stepCore at hFst
  simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc] at hFst
  try dsimp only [] at hFst
  rw [← Prog.bind_eq, exec_bind_fst, after_decodeInstr] at hFst
  cases hdec : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 with
  | error _ => rw [hdec] at hFst; simp at hFst
  | ok dec =>
    have hPure : decodeInstrPure vid pi T = .ok dec := by
      rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T]; exact hdec
    obtain ⟨hrd, hwr⟩ := hP pi dec hpc hPure
    rw [hdec] at hFst
    dsimp only [] at hFst
    let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
    have hAfterStep : after (stepCore R vid) ι T = after (dispatchInstr R vid pi dec) ιd T := by
      unfold stepCore
      simp only [Prog.bind_eq, Prog.rd_bind, after_read, hpc]
      try dsimp only []
      rw [← Prog.bind_eq, after_bind_op, hdec, after_decodeInstr]
    rw [hAfterStep] at hSigEq ⊢
    clear hFst0 hAfterStep
    unfold dispatchInstr at hFst hSigEq ⊢
    by_cases h1 : (dec.op == "=") = true
    · -- `Copy`
      simp only [h1, ite_true] at hFst hSigEq ⊢
      unfold copyRuleProg at hFst hSigEq ⊢
      match dec.rdOps, dec.wrOps, hwr, hFst, hSigEq with
      | [rd1], [wr1], hwr, hFst, hSigEq =>
        dsimp only [] at hFst hSigEq ⊢
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
            have hqPure := hq; rw [exec_resolveWrite] at hqPure
            have hqSafe := hw1.2 q hqPure
            have hqNotFC := not_frameCompleteKey_of_safe hqSafe.1 hqSafe.2
            rw [hq] at hFst; dsimp only [] at hFst
            simp only [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
            cases hv : T p with
            | none => rw [hv] at hFst; simp at hFst
            | some val =>
              rw [hv] at hFst; dsimp only [] at hFst
              simp only [Prog.wr_bind, exec_write] at hFst
              simp at hFst
              rcases hFst with ⟨rfl, rfl⟩
              rw [after_bind_op, hp, after_resolveRead] at hSigEq ⊢
              dsimp only [] at hSigEq ⊢
              rw [after_bind_op, hq, after_resolveWrite] at hSigEq ⊢
              dsimp only [] at hSigEq ⊢
              simp only [hv, Prog.bind_eq, Prog.rd_bind, after_read, Prog.wr_bind, after_write] at hSigEq ⊢
              refine commitComplete_of_le_depth hC hpc ?_ (PC.next_valid hpv) (by simp) ?_ hSigEq
              · simp [Write.apply, Write.affects, Write.newVal]
              · apply preservesFrames_of_eq_frameCompleteKey
                intro k hk
                have hkNePc : k ≠ .pcKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                have hkNeQ : k ≠ q := by rintro rfl; rw [hk] at hqNotFC; cases hqNotFC
                simp [Write.apply, Write.affects, hkNePc, hkNeQ]
      | [], _, _, hFst, _ => simp at hFst
      | _ :: _ :: _, _, _, hFst, _ => simp at hFst
      | [_], [], _, hFst, _ => simp at hFst
      | [_], _ :: _ :: _, _, hFst, _ => simp at hFst
    · simp only [h1, Bool.false_eq_true, ite_false] at hFst hSigEq ⊢
      by_cases h2 : (dec.op == "goto") = true
      · -- `Goto`
        simp only [h2, ite_true] at hFst hSigEq ⊢
        unfold gotoRuleProg at hFst hSigEq ⊢
        match dec.rdOps, dec.wrOps, hFst, hSigEq with
        | [rd1], [], hFst, hSigEq =>
          dsimp only [] at hFst hSigEq ⊢
          rw [exec_bind_fst, after_resolveRowTarget_same] at hFst
          cases hrt : (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) ιd T).1 with
          | none => rw [hrt] at hFst; simp at hFst
          | some n =>
            have hn1 := resolveRowTarget_ge_one hrt
            rw [hrt] at hFst; dsimp only [] at hFst
            simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hFst
            simp at hFst
            rcases hFst with ⟨rfl, rfl⟩
            rw [after_bind_op, hrt, after_resolveRowTarget_same] at hSigEq ⊢
            dsimp only [] at hSigEq ⊢
            simp only [Prog.bind_eq, Prog.wr_bind, after_write] at hSigEq ⊢
            refine commitComplete_of_le_depth hC hpc ?_ (PC.target_valid hpv hn1) (by simp) ?_ hSigEq
            · simp [Write.apply, Write.affects, Write.newVal]
            · apply preservesFrames_of_eq_frameCompleteKey
              intro k hk
              have hkNePc : k ≠ .pcKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
              simp [Write.apply, Write.affects, hkNePc]
        | [], _, hFst, _ => simp at hFst
        | _ :: _ :: _, _, hFst, _ => simp at hFst
        | [_], _ :: _, hFst, _ => simp at hFst
      · simp only [h2, Bool.false_eq_true, ite_false] at hFst hSigEq ⊢
        by_cases h3 : (dec.op == "br") = true
        · -- `Br`
          simp only [h3, ite_true] at hFst hSigEq ⊢
          unfold brRuleProg at hFst hSigEq ⊢
          match dec.rdOps, dec.wrOps, hFst, hSigEq with
          | [rd1, rd2, rd3], [], hFst, hSigEq =>
            dsimp only [] at hFst hSigEq ⊢
            rw [exec_bind_fst, after_resolveTruth_same] at hFst
            cases htr : (exec (resolveTruth (I := I) (E := E) vid pi rd1) ιd T).1 with
            | none => rw [htr] at hFst; simp at hFst
            | some b =>
              rw [htr] at hFst; dsimp only [] at hFst
              rw [exec_bind_fst, after_resolveRowTarget_same] at hFst
              cases hrt : (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) _ T).1 with
              | none => rw [hrt] at hFst; simp at hFst
              | some n =>
                have hn1 := resolveRowTarget_ge_one hrt
                rw [hrt] at hFst; dsimp only [] at hFst
                simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hFst
                simp at hFst
                rcases hFst with ⟨rfl, rfl⟩
                rw [after_bind_op, htr, after_resolveTruth_same] at hSigEq ⊢
                dsimp only [] at hSigEq ⊢
                rw [after_bind_op, hrt, after_resolveRowTarget_same] at hSigEq ⊢
                dsimp only [] at hSigEq ⊢
                simp only [Prog.bind_eq, Prog.wr_bind, after_write] at hSigEq ⊢
                refine commitComplete_of_le_depth hC hpc ?_ (PC.target_valid hpv hn1) (by simp) ?_ hSigEq
                · simp [Write.apply, Write.affects, Write.newVal]
                · apply preservesFrames_of_eq_frameCompleteKey
                  intro k hk
                  have hkNePc : k ≠ .pcKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                  simp [Write.apply, Write.affects, hkNePc]
          | [], _, hFst, _ => simp at hFst
          | [_], _, hFst, _ => simp at hFst
          | [_, _], _, hFst, _ => simp at hFst
          | _ :: _ :: _ :: _ :: _, _, hFst, _ => simp at hFst
          | [_, _, _], _ :: _, hFst, _ => simp at hFst
        · simp only [h3, Bool.false_eq_true, ite_false] at hFst hSigEq ⊢
          by_cases h4 : (dec.op == "call") = true
          · -- `Call`
            simp only [h4, ite_true] at hFst ⊢
            clear hSigEq
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
                    rw [after_bind_op, hp, after_resolveRead]; dsimp only []
                    simp only [Prog.bind_eq, Prog.rd_bind, after_read, hvFn]; try dsimp only []
                    rw [← Prog.bind_eq, after_bind_op, hb]; try dsimp only []
                    simp only [Prog.wr_bind, after_write]
                    exact commitComplete_after_bindChild_and_entry hC hpc hpv hb
          · simp only [h4, Bool.false_eq_true, ite_false] at hFst hSigEq ⊢
            by_cases h5 : (dec.op == "return") = true
            · -- `Return`
              simp only [h5, ite_true] at hFst hSigEq ⊢
              unfold returnRuleProg at hFst hSigEq ⊢
              match dec.rdOps, dec.wrOps, hFst, hSigEq with
              | [], [], hFst, hSigEq =>
                dsimp only [] at hFst hSigEq ⊢
                by_cases hd : 1 < pi.depth
                · simp only [hd, ite_true] at hFst hSigEq ⊢
                  rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
                  simp only [Prog.bind_eq, Prog.rd_bind, after_read] at hSigEq ⊢
                  cases hvRet : T (.frameRetPc vid pi.depth) with
                  | none => rw [hvRet] at hFst; simp at hFst
                  | some vRet =>
                    rw [hvRet] at hFst hSigEq
                    cases vRet <;> try (simp at hFst; done)
                    rename_i r
                    dsimp only [] at hFst hSigEq ⊢
                    cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) with
                    | false => rw [hrv] at hFst; simp at hFst
                    | true =>
                      rw [hrv] at hFst hSigEq
                      simp only [ite_true, Prog.bind_eq, Prog.wr_bind, exec_write] at hFst
                      simp only [ite_true, Prog.wr_bind, after_write] at hSigEq ⊢
                      simp at hFst
                      rcases hFst with ⟨rfl, rfl⟩
                      simp only [Bool.and_eq_true, decide_eq_true_eq] at hrv
                      refine commitComplete_of_le_depth hC hpc ?_ hrv.1 (by omega) ?_ hSigEq
                      · simp [Write.apply, Write.affects, Write.newVal, inFrame]
                      · refine ⟨?_, ?_⟩
                        · intro e _ hle
                          have hne : e ≠ pi.depth := by omega
                          refine ⟨?_, ?_, fun dir j => ?_⟩
                          · simp [Write.apply, Write.affects, inFrame, hne]
                          · simp [Write.apply, Write.affects, inFrame, hne]
                          · simp [Write.apply, Write.affects, inFrame, hne]
                        · intro e _ hle
                          have hne : e ≠ pi.depth := by omega
                          simp [Write.apply, Write.affects, inFrame, hne]
                · simp only [hd, ite_false, Prog.bind_eq, Prog.wr_bind, exec_write] at hFst
                  simp only [hd, ite_false, Prog.bind_eq, Prog.wr_bind, after_write] at hSigEq ⊢
                  simp at hFst
                  rcases hFst with ⟨rfl, rfl⟩
                  refine commitComplete_of_le_depth hC hpc ?_ hpv (Nat.le_refl _) ?_ hSigEq
                  · simp [Write.apply, Write.affects, hpc]
                  · apply preservesFrames_of_eq_frameCompleteKey
                    intro k hk
                    have hkNeSt : k ≠ .statusKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                    simp [Write.apply, Write.affects, hkNeSt]
              | _ :: _, _, hFst, _ => simp at hFst
              | [], _ :: _, hFst, _ => simp at hFst
            · simp only [h5, Bool.false_eq_true, ite_false] at hFst hSigEq ⊢
              by_cases h6 : R.caps dec.op = true
              · -- `Native`
                simp only [h6, ite_true] at hFst hSigEq ⊢
                have hH1 := hR.nativeH1 dec.op h6
                unfold nativeRuleProg at hFst hSigEq ⊢
                rw [exec_bind_fst, after_resolveAllRead_same] at hFst
                cases hrdK : (exec (resolveAllRead (I := I) (E := E) vid pi dec.rdOps) ιd T).1 with
                | none => rw [hrdK] at hFst; simp at hFst
                | some rdKeys =>
                  rw [hrdK] at hFst; dsimp only [] at hFst
                  rw [exec_bind_fst, after_resolveAllWrite_same] at hFst
                  cases hwrK : (exec (resolveAllWrite (I := I) (E := E) vid pi dec.wrOps) _ T).1 with
                  | none => rw [hwrK] at hFst; simp at hFst
                  | some wrKeys =>
                    have hwrSafe := resolveAllWrite_safe_list hwr hwrK
                    rw [hwrK] at hFst; dsimp only [] at hFst
                    rw [exec_bind_fst, after_readKeyList_same] at hFst
                    rw [exec_bind_fst, after_recvMany_same] at hFst
                    cases hEv : (R.native dec.op).eval wrKeys _ _ with
                    | error _ => rw [hEv] at hFst; simp at hFst
                    | ok out =>
                      obtain ⟨hSub, _⟩ := hH1 wrKeys _ _ out hEv
                      have hOutSafe : ∀ k v, (k, v) ∈ out.writes →
                          ForbiddenKey vid pi.depth k = false ∧ isFrameParamKey vid k = false :=
                        fun k v hkv => hwrSafe k (hSub k v hkv)
                      rw [hEv] at hFst; dsimp only [] at hFst
                      rw [exec_bind_fst, after_emitAll_same, exec_bind_fst] at hFst
                      rw [after_bind_op, hrdK, after_resolveAllRead_same] at hSigEq ⊢
                      dsimp only [] at hSigEq ⊢
                      rw [after_bind_op, hwrK, after_resolveAllWrite_same] at hSigEq ⊢
                      dsimp only [] at hSigEq ⊢
                      rw [after_bind_op, after_readKeyList_same,
                          after_bind_op, after_recvMany_same, hEv] at hSigEq ⊢
                      dsimp only [] at hSigEq ⊢
                      rw [after_bind_op, after_emitAll_same, after_bind_op] at hSigEq ⊢
                      simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hFst
                      simp only [Prog.bind_eq, Prog.wr_bind, after_write] at hSigEq ⊢
                      cases hSet : out.setStatus with
                      | none =>
                        rw [hSet] at hFst hSigEq
                        dsimp only [] at hFst hSigEq ⊢
                        simp only [Prog.pure_eq, after_ret] at hSigEq ⊢
                        simp at hFst
                        rcases hFst with ⟨rfl, rfl⟩
                        refine commitComplete_of_le_depth hC hpc ?_ (PC.next_valid hpv) (by simp) ?_ hSigEq
                        · simp [Write.apply, Write.affects, Write.newVal]
                        · apply preservesFrames_of_eq_frameCompleteKey
                          intro k hk
                          have hkNePc : k ≠ .pcKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                          simp only [Write.apply, Write.affects, decide_eq_true_eq, hkNePc, ite_false]
                          exact putAll_preserves_frameCompleteKey hOutSafe _ T hk
                      | some s =>
                        rw [hSet] at hFst hSigEq
                        dsimp only [] at hFst hSigEq ⊢
                        simp only [exec_write] at hFst
                        simp only [after_write, Prog.pure_eq, after_ret] at hSigEq ⊢
                        simp at hFst
                        rcases hFst with ⟨rfl, rfl⟩
                        refine commitComplete_of_le_depth hC hpc ?_ (PC.next_valid hpv) (by simp) ?_ hSigEq
                        · simp [Write.apply, Write.affects, Write.newVal]
                        · apply preservesFrames_of_eq_frameCompleteKey
                          intro k hk
                          have hkNePc : k ≠ .pcKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                          have hkNeSt : k ≠ .statusKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                          simp only [Write.apply, Write.affects, decide_eq_true_eq, hkNePc, hkNeSt, ite_false]
                          exact putAll_preserves_frameCompleteKey hOutSafe _ T hk
              · -- `Deleg`, `Handoff`, or `UserCall`
                simp only [h6, Bool.false_eq_true, ite_false] at hFst hSigEq ⊢
                have nonDelegCase : ∀ ιh,
                    (exec (do
                      let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                      match vHdr with
                      | some (.routeHdr _) =>
                          handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                      | some .rwfuncHdr =>
                          userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                      | _ => pure (.error "unmatched_opcode")) ιh T).1 = .ok (pi', evs) →
                    (∀ fn, after (do
                      let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                      match vHdr with
                      | some (.routeHdr _) =>
                          handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                      | some .rwfuncHdr =>
                          userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                      | _ => pure (.error "unmatched_opcode")) ιh T (.libSig fn) = T (.libSig fn)) →
                    CommitComplete vid (after (do
                      let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                      match vHdr with
                      | some (.routeHdr _) =>
                          handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                      | some .rwfuncHdr =>
                          userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                      | _ => pure (.error "unmatched_opcode")) ιh T) := by
                  intro ιh hOkH hSigH
                  rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hOkH
                  simp only [Prog.bind_eq, Prog.rd_bind, after_read] at hSigH ⊢
                  cases hvHdr : T (.libHeader dec.op) with
                  | none => rw [hvHdr] at hOkH; simp at hOkH
                  | some vHdr =>
                    rw [hvHdr] at hOkH hSigH
                    cases vHdr <;> try (simp at hOkH; done)
                    · -- `UserCall`
                      dsimp only [] at hOkH ⊢
                      clear hSigH
                      unfold userCallRuleProg at hOkH ⊢
                      rw [exec_bind_fst] at hOkH
                      cases hb : (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ιh T).1 with
                      | error _ => rw [hb] at hOkH; simp at hOkH
                      | ok u =>
                        cases u; rw [hb] at hOkH; dsimp only [] at hOkH
                        simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hOkH
                        simp at hOkH
                        rcases hOkH with ⟨rfl, rfl⟩
                        rw [after_bind_op, hb]; dsimp only []
                        simp only [Prog.bind_eq, Prog.wr_bind, after_write]
                        exact commitComplete_after_bindChild_and_entry hC hpc hpv hb
                    · -- `Handoff`
                      dsimp only [] at hOkH hSigH ⊢
                      have hExt := hR.handoffH2 dec.op
                      unfold handoffRuleProg at hOkH hSigH ⊢
                      rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hOkH
                      simp only [Prog.bind_eq, Prog.rd_bind, after_read] at hSigH ⊢
                      cases hq : T (.queueEntry dec.op vid) with
                      | some _ => rw [hq] at hOkH; simp at hOkH
                      | none =>
                        rw [hq] at hOkH hSigH; dsimp only [] at hOkH hSigH ⊢
                        simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hOkH
                        simp only [Prog.wr_bind, after_write] at hSigH ⊢
                        rw [← Prog.bind_eq, exec_bind_fst, after_ext_same hExt] at hOkH
                        cases hex : (exec ((R.handoffExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                        | error _ => rw [hex] at hOkH; simp at hOkH
                        | ok out =>
                          have hPcV := hExt.pcValid vid pi dec.rdOps dec.wrOps _ _ out hpv hex
                          have hOutSafe : ∀ k v, (k, v) ∈ out.writes →
                              ForbiddenKey vid pi.depth k = false ∧ isFrameParamKey vid k = false :=
                            fun k v hm => hExt.writesSafe vid pi dec.rdOps dec.wrOps _ _ out k v hex hm
                          rw [hex] at hOkH; dsimp only [] at hOkH
                          rw [← Prog.bind_eq, exec_bind_fst, after_emitAll_same,
                              ← Prog.bind_eq, exec_bind_fst] at hOkH
                          simp at hOkH
                          rcases hOkH with ⟨rfl, rfl⟩
                          rw [← Prog.bind_eq, after_bind_op, hex, after_ext_same hExt] at hSigH ⊢
                          dsimp only [] at hSigH ⊢
                          rw [← Prog.bind_eq, after_bind_op, after_emitAll_same,
                              ← Prog.bind_eq, after_bind_op] at hSigH ⊢
                          simp only [after_write, Prog.pure_eq, after_ret] at hSigH ⊢
                          refine commitComplete_of_le_depth hC hpc ?_ hPcV.2.1
                            (Nat.le_of_eq hPcV.2.2) ?_ hSigH
                          · simp [Write.apply, Write.affects, Write.newVal]
                          · apply preservesFrames_of_eq_frameCompleteKey
                            intro k hk
                            have hkNePc : k ≠ .pcKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                            have hkNeQ : k ≠ .queueEntry dec.op vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                            simp only [Write.apply, Write.affects, decide_eq_true_eq, hkNePc, hkNeQ, ite_false]
                            rw [putAll_preserves_frameCompleteKey hOutSafe _ _ hk]
                            simp [Write.apply, Write.affects, hkNeQ]
                rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
                simp only [Prog.bind_eq, Prog.rd_bind, after_read] at hSigEq ⊢
                cases hvDel : T (.delegEntry dec.op) with
                | none =>
                  rw [hvDel] at hFst hSigEq
                  exact nonDelegCase _ hFst hSigEq
                | some vDel =>
                  rw [hvDel] at hFst hSigEq
                  cases vDel <;> try exact nonDelegCase _ hFst hSigEq
                  -- `Deleg`
                  dsimp only [] at hFst hSigEq ⊢
                  have hExt := hR.delegH2 dec.op
                  unfold delegRuleProg at hFst hSigEq ⊢
                  rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFst
                  simp only [Prog.bind_eq, Prog.rd_bind, after_read] at hSigEq ⊢
                  cases hq : T (.queueEntry dec.op vid) with
                  | some _ => rw [hq] at hFst; simp at hFst
                  | none =>
                    rw [hq] at hFst hSigEq; dsimp only [] at hFst hSigEq ⊢
                    simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hFst
                    simp only [Prog.wr_bind, after_write] at hSigEq ⊢
                    rw [← Prog.bind_eq, exec_bind_fst, after_ext_same hExt] at hFst
                    cases hex : (exec ((R.delegExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                    | error _ => rw [hex] at hFst; simp at hFst
                    | ok out =>
                      have hPcV := hExt.pcValid vid pi dec.rdOps dec.wrOps _ _ out hpv hex
                      have hOutSafe : ∀ k v, (k, v) ∈ out.writes →
                          ForbiddenKey vid pi.depth k = false ∧ isFrameParamKey vid k = false :=
                        fun k v hm => hExt.writesSafe vid pi dec.rdOps dec.wrOps _ _ out k v hex hm
                      rw [hex] at hFst; dsimp only [] at hFst
                      rw [← Prog.bind_eq, exec_bind_fst, after_emitAll_same,
                          ← Prog.bind_eq, exec_bind_fst] at hFst
                      simp at hFst
                      rcases hFst with ⟨rfl, rfl⟩
                      rw [← Prog.bind_eq, after_bind_op, hex, after_ext_same hExt] at hSigEq ⊢
                      dsimp only [] at hSigEq ⊢
                      rw [← Prog.bind_eq, after_bind_op, after_emitAll_same,
                          ← Prog.bind_eq, after_bind_op] at hSigEq ⊢
                      simp only [after_write, Prog.pure_eq, after_ret] at hSigEq ⊢
                      refine commitComplete_of_le_depth hC hpc ?_ hPcV.2.1
                        (Nat.le_of_eq hPcV.2.2) ?_ hSigEq
                      · simp [Write.apply, Write.affects, Write.newVal]
                      · apply preservesFrames_of_eq_frameCompleteKey
                        intro k hk
                        have hkNePc : k ≠ .pcKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                        have hkNeSt : k ≠ .statusKey vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                        have hkNeQ : k ≠ .queueEntry dec.op vid := by rintro rfl; simp [isFrameCompleteKey] at hk
                        simp only [Write.apply, Write.affects, decide_eq_true_eq, hkNePc, hkNeSt, hkNeQ, ite_false]
                        rw [putAll_preserves_frameCompleteKey hOutSafe _ _ hk]
                        simp [Write.apply, Write.affects, hkNeSt, hkNeQ]

/-- Single `gatedStep` preservation of **Invariant 2 (`CommitComplete`)** across
all outcomes (`.paused`, `.failed`, and `.stepped`). -/
theorem commitComplete_gatedStep {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)}
    (hC : CommitComplete vid T)
    (hP : StepDiscipline vid T)
    {ι : Nat → I}
    (hSigSafe : T (.stopKey vid) = none → LibSigSafe R vid ι T) :
    CommitComplete vid (after (gatedStep R vid) ι T) := by
  cases hrho : T (.stopKey vid) with
  | some v =>
    have hAf : after (gatedStep R vid) ι T =
        after (pauseAfterGate (D := D) (I := I) (E := E) vid) ι T := by
      simp [gatedStep, after, hrho]
    rw [hAf]
    exact hC.of_storeAgree (storeAgree_pauseAfterGate vid ι T).symm
  | none =>
    cases hCore : exec (stepCore R vid) ι T with
    | mk res asCore =>
      cases res with
      | error err =>
        have hFail : (exec (stepCore R vid) ι T).1 = .error err := by rw [hCore]
        exact hC.of_storeAgree (failed_gatedStep_preserves_agree hR hrho hFail).symm
      | ok pair =>
        rcases pair with ⟨pi', evs⟩
        have hAf : after (gatedStep R vid) ι T = after (stepCore R vid) ι T := by
          unfold gatedStep
          simp only [Prog.bind_eq, Prog.rd_bind, after_read, hrho]
          rw [after_bind]
          unfold after at *
          rw [hCore]
          rfl
        rw [hAf]
        exact commitComplete_stepCore_ok hR hC hP (hSigSafe hrho) hCore

/-- **Invariant 2 along Uninterrupted Commit Sequence (`commitComplete_commitPointAt`)**:
every commit point `Tk` reached from a `CommitComplete` initial store `T₀`
satisfies `CommitComplete vid Tk`. -/
theorem commitComplete_commitPointAt {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ : Store Key (Val D)}
    (hC₀ : CommitComplete vid T₀)
    (hP : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    (hSig : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → Tk (.stopKey vid) = none →
      LibSigSafe R vid (ιs k) Tk) :
    ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk → CommitComplete vid Tk := by
  intro k
  induction k with
  | zero =>
    intro Tk hcp
    simp only [commitPointAt, Option.some.injEq] at hcp
    subst hcp
    exact hC₀.of_storeAgree (storeAgree_initCommit vid T₀).symm
  | succ k ih =>
    intro Tk1 hcp1
    simp only [commitPointAt] at hcp1
    cases hcp : commitPointAt R vid ιs T₀ k with
    | none => rw [hcp] at hcp1; simp at hcp1
    | some Tk =>
      rw [hcp] at hcp1
      dsimp only [] at hcp1
      cases hrunB : isStoreRunnableBool vid Tk with
      | false => rw [hrunB] at hcp1; simp at hcp1
      | true =>
        rw [hrunB] at hcp1
        simp only [ite_true] at hcp1
        have hRunTk : StoreRunnable vid Tk := (isStoreRunnableBool_iff vid Tk).1 hrunB
        cases hout : (exec (gatedStep R vid) (ιs k) Tk).1 with
        | paused => rw [hout] at hcp1; simp at hcp1
        | failed _ => rw [hout] at hcp1; simp at hcp1
        | stepped pi' evs =>
          rw [hout] at hcp1
          simp only [Option.some.injEq] at hcp1
          subst hcp1
          have hCTk := ih Tk hcp
          have hPTk := hP k Tk hcp hRunTk
          have hCRaw := commitComplete_gatedStep (E := E) hR hCTk hPTk (hSig k Tk hcp hRunTk)
          exact hCRaw.of_storeAgree (storeAgree_autoResumePaused vid _).symm

/-- **Invariant 2 across Arbitrary Interrupted Runs (`inv:commit`, `commitComplete_resumableRun`)**:
starting from any `vthreadCreate` (or `CommitComplete` + `ValidCommitPoint`) store `T₀`,
every store `S` reached by any `ResumableRun` (including mid-step `stop`s, `Pause`s,
step failures, `createIfAbsent` lease acquisitions/releases, and `resume`s)
satisfies `CommitComplete vid S`. -/
theorem commitComplete_resumableRun {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {ιs : Nat → Nat → I} {T₀ : Store Key (Val D)}
    (hInit : ValidCommitPoint vid T₀)
    (hC₀ : CommitComplete vid T₀)
    (hP : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → StepDiscipline vid Tk)
    (hSig : ∀ k Tk, commitPointAt R vid ιs T₀ k = some Tk →
      StoreRunnable vid Tk → Tk (.stopKey vid) = none →
      LibSigSafe R vid (ιs k) Tk)
    {tr : List (StepLabel E)} {S : Store Key (Val D)}
    (hRun : ResumableRun R vid ιs T₀ tr S) :
    CommitComplete vid S := by
  obtain ⟨_, _, Tk, hcp, hAg, _⟩ := stop_resume_resumption hR hInit hP hRun
  have hCTk := commitComplete_commitPointAt hR hC₀ hP hSig tr.length Tk hcp
  exact hCTk.of_storeAgree hAg.symm

/-- Concrete counterexample showing `LibSigSafe` is necessary for `CommitComplete`:
a `Copy` step satisfying `(P)` (`StepDiscipline`) that overwrites `/lib/main/[0,0]`
from `sig(0, 0)` to `sig(1, 0)` while `main` is active at frame `[1]` breaks
`CommitComplete` because `[1]/[0, -1]` holds `none`. -/
theorem commitComplete_broken_by_libSig_overwrite :
    ∃ (R : Runtime Unit Unit Unit) (T₀ : Store Key (Val Unit)),
      RuntimeSpec R ∧
      ValidCommitPoint 1 T₀ ∧
      CommitComplete 1 T₀ ∧
      StepDiscipline 1 T₀ ∧
      (exec (gatedStep R 1) (fun _ => ()) T₀).1 = .stepped ⟨1, 2⟩ [] ∧
      ¬ CommitComplete 1 (after (gatedStep R 1) (fun _ => ()) T₀) := by
  let R : Runtime Unit Unit Unit :=
    { caps := fun _ => false
      native := fun _ => ⟨0, fun _ _ _ => .error "none"⟩
      delegExt := fun _ => ⟨fun _ _ _ _ => pure (.error "none")⟩
      handoffExt := fun _ => ⟨fun _ _ _ _ => pure (.error "none")⟩ }
  have hExtSpec : ExtensionSpec (D := Unit) (I := Unit) (E := Unit)
      ⟨fun _ _ _ _ => pure (.error "none")⟩ :=
    { internalPure := fun _ _ _ _ _ _ => ⟨rfl, rfl⟩
      readsSafe := fun _ _ _ _ _ _ _ _ h => by simp at h
      writesSafe := fun _ _ _ _ _ _ _ _ _ h => by simp at h
      pcValid := fun _ _ _ _ _ _ _ _ h => by simp at h }
  have hR : RuntimeSpec R :=
    { capsDisjointCore := fun _ _ => rfl
      nativeH1 := fun _ h => by cases h
      delegH2 := fun _ => hExtSpec
      handoffH2 := fun _ => hExtSpec }
  let T_base : Store Key (Val Unit) := fun k =>
    if k == .libSig "main" then some (.sig 0 0)
    else if k == .libInstr "main" 1 then some (.instr "=" 1 1)
    else if k == .libOperand "main" 1 .rd 1 then some (.sig 1 0)
    else if k == .libOperand "main" 1 .wr 1 then some (.ref 0 (.absKey (.libSig "main")))
    else none
  let T₀ := vthreadCreate 1 "main" [] [] T_base
  refine ⟨R, T₀, hR, vthreadCreate_validCommitPoint 1 "main" [] [] T_base,
    vthreadCreate_commitComplete 1 "main" [] [] T_base (by simp [T_base]), ?_, ?_, ?_⟩
  · -- `StepDiscipline 1 T₀`
    intro pi dec hpc hdec
    have hpc0 : T₀ (.pcKey 1) = some (.pc ⟨1, 1⟩) := vthreadCreate_pc 1 "main" [] [] T_base
    rw [hpc0] at hpc; cases hpc
    have hLib1 : T₀ (.frameLib 1 1) = some (.sym "main") := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, Write.newVal]
    have hIns1 : T₀ (.libInstr "main" 1) = some (.instr "=" 1 1) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    have hOpR : T₀ (.libOperand "main" 1 .rd 1) = some (.sig 1 0) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    have hOpW : T₀ (.libOperand "main" 1 .wr 1) = some (.ref 0 (.absKey (.libSig "main"))) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    have hdec0 : decodeInstrPure 1 ⟨1, 1⟩ T₀ =
        .ok ⟨"main", "=", [(.libOperand "main" 1 .rd 1, .sig 1 0)],
             [(.libOperand "main" 1 .wr 1, .ref 0 (.absKey (.libSig "main")))]⟩ := by
      simp [decodeInstrPure, PC.valid, hLib1, hIns1, readOperandsPure, hOpR, hOpW]
    rw [hdec0] at hdec; cases hdec
    refine ⟨?_, ?_⟩
    · intro x hx
      simp only [List.mem_singleton] at hx; subst hx
      refine ⟨by simp [resolveReadReads], ?_⟩
      intro p hp; simp [resolveReadPure] at hp; subst hp
      rfl
    · intro y hy
      simp only [List.mem_singleton] at hy; subst hy
      refine ⟨by simp [resolveWriteReads, derefChainReads], ?_⟩
      intro q hq; simp [resolveWritePure, derefChainPure, BaseRef.startKey] at hq; subst hq
      exact ⟨rfl, rfl⟩
  · -- Step succeeds with `.stepped ⟨1, 2⟩ []`
    have hStop : T₀ (.stopKey 1) = none := vthreadCreate_stop 1 "main" [] [] T_base
    have hPc : T₀ (.pcKey 1) = some (.pc ⟨1, 1⟩) := vthreadCreate_pc 1 "main" [] [] T_base
    have hLib1 : T₀ (.frameLib 1 1) = some (.sym "main") := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, Write.newVal]
    have hIns1 : T₀ (.libInstr "main" 1) = some (.instr "=" 1 1) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    have hOpR : T₀ (.libOperand "main" 1 .rd 1) = some (.sig 1 0) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    have hOpW : T₀ (.libOperand "main" 1 .wr 1) = some (.ref 0 (.absKey (.libSig "main"))) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    simp [gatedStep, hStop, stepCore, hPc, decodeInstr, PC.valid, hLib1, hIns1,
      readOperandsFrom, hOpR, hOpW, dispatchInstr, copyRuleProg,
      resolveRead, resolveWrite, derefChain, BaseRef.startKey, finalizeStep,
      Prog.bind_eq, Prog.rd_bind, Prog.wr_bind, exec, PC.next]
  · -- Post-step store violates `CommitComplete` because `/lib/main/[0,0] = sig(1, 0)`
    -- while `[1]/[0, -1] = none`.
    intro hC1
    obtain ⟨pi', hpc', _, hFrames', _⟩ := hC1
    have hStop : T₀ (.stopKey 1) = none := vthreadCreate_stop 1 "main" [] [] T_base
    have hPc : T₀ (.pcKey 1) = some (.pc ⟨1, 1⟩) := vthreadCreate_pc 1 "main" [] [] T_base
    have hLib1 : T₀ (.frameLib 1 1) = some (.sym "main") := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, Write.newVal]
    have hIns1 : T₀ (.libInstr "main" 1) = some (.instr "=" 1 1) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    have hOpR : T₀ (.libOperand "main" 1 .rd 1) = some (.sig 1 0) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    have hOpW : T₀ (.libOperand "main" 1 .wr 1) = some (.ref 0 (.absKey (.libSig "main"))) := by
      simp [T₀, vthreadCreate, vthreadCreateWrites, paramPtrWrites,
        Write.apply, Write.affects, isFrameOfVid, T_base]
    have hAf : after (gatedStep R 1) (fun _ => ()) T₀ =
        (Write.put (.pcKey 1) (.pc ⟨1, 2⟩)).apply
          ((Write.put (.libSig "main") (.sig 1 0)).apply T₀) := by
      simp [gatedStep, hStop, stepCore, hPc, decodeInstr, PC.valid, hLib1, hIns1,
        readOperandsFrom, hOpR, hOpW, dispatchInstr, copyRuleProg,
        resolveRead, resolveWrite, derefChain, BaseRef.startKey, finalizeStep,
        Prog.bind_eq, Prog.rd_bind, Prog.wr_bind, after, exec, PC.next]
    rw [hAf] at hpc' hFrames'
    have hpc2 : (Write.put (.pcKey 1) (.pc ⟨1, 2⟩)).apply
        ((Write.put (.libSig "main") (.sig 1 0)).apply T₀) (.pcKey 1) =
        some (.pc ⟨1, 2⟩) := by
      simp [Write.apply, Write.affects, Write.newVal]
    rw [hpc2] at hpc'; cases hpc'
    obtain ⟨⟨fn, nr, nw, hLib', hSig', hRd', _⟩, _⟩ := hFrames' 1 (Nat.le_refl 1) (Nat.le_refl 1)
    have hLibEq : (Write.put (.pcKey 1) (.pc ⟨1, 2⟩)).apply
        ((Write.put (.libSig "main") (.sig 1 0)).apply T₀) (.frameLib 1 1) =
        some (.sym "main") := by
      simp [Write.apply, Write.affects, hLib1]
    rw [hLibEq] at hLib'; cases hLib'
    have hSigEq : (Write.put (.pcKey 1) (.pc ⟨1, 2⟩)).apply
        ((Write.put (.libSig "main") (.sig 1 0)).apply T₀) (.libSig "main") =
        some (.sig 1 0) := by
      simp [Write.apply, Write.affects, Write.newVal]
    rw [hSigEq] at hSig'; cases hSig'
    obtain ⟨p, hp1⟩ := hRd' 1 (Nat.le_refl 1) (Nat.le_refl 1)
    have hpNone : (Write.put (.pcKey 1) (.pc ⟨1, 2⟩)).apply
        ((Write.put (.libSig "main") (.sig 1 0)).apply T₀) (.frameParam 1 1 .rd 1) =
        none := by
      simp [Write.apply, Write.affects, Write.newVal, T₀, vthreadCreate,
        vthreadCreateWrites, paramPtrWrites, isFrameOfVid]
    rw [hpNone] at hp1
    cases hp1

end CommitPreservation

/-! ## Proposition 6: Fail-Stop (`prop:failstop`) -/

section FailStop

variable {D I E : Type}

/-- Configuration `<T, vid>` does not dispatch to `Deleg` or `Handoff`
(hypothesis of Proposition 6 in Section 5). -/
def NotHandoffOrDeleg (R : Runtime D I E) (vid : Vid)
    (T : Store Key (Val D)) : Prop :=
  ∀ pi dec,
    T (.pcKey vid) = some (.pc pi) →
    decodeInstrPure vid pi T = .ok dec →
    ¬ RuleGuard R T dec.op pi .deleg ∧ ¬ RuleGuard R T dec.op pi .handoff

private theorem isCoreOp_false_of_ne {op : SymName}
    (h1 : (op == "=") = false)
    (h2 : (op == "goto") = false)
    (h3 : (op == "br") = false)
    (h4 : (op == "call") = false)
    (h5 : (op == "return") = false) :
    isCoreOp op = false := by
  simp [isCoreOp, h1, h2, h3, h4, h5]

/-- Core lemma for Proposition 6 without `H2`: when `NotHandoffOrDeleg R vid T`
holds, any failing `stepCore` attempt (even from an arbitrarily malformed store,
without assuming `(P)`) leaves every key outside dead frames (`isDeadFrameKey vid
(liveDepthOf vid T)`) completely unchanged. -/
private theorem failed_stepCore_nonHandoff_eq {R : Runtime D I E}
    {vid : Vid} {T : Store Key (Val D)} {ι : Nat → I} {err : ErrMsg}
    (hNotHD : NotHandoffOrDeleg R vid T)
    (hFail : (exec (stepCore R vid) ι T).1 = .error err)
    {k : Key} (hk : isDeadFrameKey vid (liveDepthOf vid T) k = false) :
    after (stepCore R vid) ι T k = T k := by
  unfold stepCore at hFail ⊢
  simp only [Prog.bind_eq, Prog.rd_bind] at hFail ⊢
  rw [exec_read] at hFail
  rw [after_read]
  cases hpc : T (.pcKey vid) with
  | none => rfl
  | some vPc =>
    rw [hpc] at hFail
    cases vPc <;> try rfl
    rename_i pi
    have hd : liveDepthOf vid T = pi.depth := liveDepthOf_pc hpc
    dsimp only [] at hFail ⊢
    rw [← Prog.bind_eq, exec_bind_fst, after_decodeInstr] at hFail
    rw [← Prog.bind_eq, after_bind_op, after_decodeInstr]
    cases hdec : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 with
    | error _ => rfl
    | ok dec =>
      have hPure : decodeInstrPure vid pi T = .ok dec := by
        rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T]; exact hdec
      obtain ⟨hNoDeleg, hNoHandoff⟩ := hNotHD pi dec hpc hPure
      rw [hdec] at hFail
      dsimp only [] at hFail ⊢
      let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
      change (exec (dispatchInstr R vid pi dec) ιd T).1 = .error err at hFail
      change after (dispatchInstr R vid pi dec) ιd T k = T k
      have hkNotChild : inFrame vid (pi.depth + 1) k = false := by
        cases hin : inFrame vid (pi.depth + 1) k with
        | false => rfl
        | true =>
          have hdead := inFrame_iff_dead_of_gt (by omega : pi.depth < pi.depth + 1) hin
          rw [← hd, hk] at hdead
          cases hdead
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
          | none => rfl
          | some p =>
            rw [hp] at hFail; dsimp only [] at hFail ⊢
            rw [exec_bind_fst, after_resolveWrite] at hFail
            rw [after_bind_op, after_resolveWrite]
            cases hq : (exec (resolveWrite (I := I) (E := E) vid pi wr1) _ T).1 with
            | none => rfl
            | some q =>
              rw [hq] at hFail; dsimp only [] at hFail ⊢
              simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hFail ⊢
              cases hpVal : T p with
              | none => rfl
              | some val =>
                rw [hpVal] at hFail
                simp [Prog.wr_bind, exec_write, exec] at hFail
        | [], _, _ => rfl
        | _ :: _ :: _, _, _ => rfl
        | [_], [], _ => rfl
        | [_], _ :: _ :: _, _ => rfl
      · simp only [Bool.not_eq_true] at h1
        simp only [h1, Bool.false_eq_true, ite_false] at hFail ⊢
        by_cases h2 : (dec.op == "goto") = true
        · simp only [h2, ite_true] at hFail ⊢
          unfold gotoRuleProg at hFail ⊢
          match dec.rdOps, dec.wrOps, hFail with
          | [rd1], [], hFail =>
            dsimp only [] at hFail ⊢
            rw [exec_bind_fst, after_resolveRowTarget_same] at hFail
            rw [after_bind_op, after_resolveRowTarget_same]
            cases hrt : (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) ιd T).1 with
            | none => rfl
            | some n =>
              rw [hrt] at hFail
              simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hFail
          | [], _, _ => rfl
          | _ :: _ :: _, _, _ => rfl
          | [_], _ :: _, _ => rfl
        · simp only [Bool.not_eq_true] at h2
          simp only [h2, Bool.false_eq_true, ite_false] at hFail ⊢
          by_cases h3 : (dec.op == "br") = true
          · simp only [h3, ite_true] at hFail ⊢
            unfold brRuleProg at hFail ⊢
            match dec.rdOps, dec.wrOps, hFail with
            | [rd1, rd2, rd3], [], hFail =>
              dsimp only [] at hFail ⊢
              rw [exec_bind_fst, after_resolveTruth_same] at hFail
              rw [after_bind_op, after_resolveTruth_same]
              cases htr : (exec (resolveTruth (I := I) (E := E) vid pi rd1) ιd T).1 with
              | none => rfl
              | some b =>
                rw [htr] at hFail; dsimp only [] at hFail ⊢
                rw [exec_bind_fst, after_resolveRowTarget_same] at hFail
                rw [after_bind_op, after_resolveRowTarget_same]
                cases hrt : (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) _ T).1 with
                | none => rfl
                | some n =>
                  rw [hrt] at hFail
                  simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hFail
            | [], _, _ => rfl
            | [_], _, _ => rfl
            | [_, _], _, _ => rfl
            | _ :: _ :: _ :: _ :: _, _, _ => rfl
            | [_, _, _], _ :: _, _ => rfl
          · simp only [Bool.not_eq_true] at h3
            simp only [h3, Bool.false_eq_true, ite_false] at hFail ⊢
            by_cases h4 : (dec.op == "call") = true
            · simp only [h4, ite_true] at hFail ⊢
              unfold callRuleProg at hFail ⊢
              match dec.rdOps, hFail with
              | [], _ => rfl
              | fnOp :: argRdOps, hFail =>
                dsimp only [] at hFail ⊢
                rw [exec_bind_fst, after_resolveRead] at hFail
                rw [after_bind_op, after_resolveRead]
                cases hp : (exec (resolveRead (I := I) (E := E) vid pi fnOp) ιd T).1 with
                | none => rfl
                | some pFn =>
                  rw [hp] at hFail; dsimp only [] at hFail ⊢
                  simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hFail ⊢
                  cases hvFn : T pFn with
                  | none => rfl
                  | some vFn =>
                    rw [hvFn] at hFail
                    cases vFn <;> try rfl
                    rename_i callee
                    dsimp only [] at hFail ⊢
                    rw [← Prog.bind_eq, exec_bind_fst] at hFail
                    rw [← Prog.bind_eq, after_bind_op]
                    cases hb : (exec (bindChild (I := I) (E := E) vid pi callee argRdOps dec.wrOps) _ T).1 with
                    | error _ =>
                      exact (bindChild_only_writes_child (I := I) (E := E)
                        vid pi callee argRdOps dec.wrOps _ T).after_eq hkNotChild
                    | ok u =>
                      rw [hb] at hFail; cases u
                      simp [Prog.wr_bind, exec_write, exec] at hFail
            · simp only [Bool.not_eq_true] at h4
              simp only [h4, Bool.false_eq_true, ite_false] at hFail ⊢
              by_cases h5 : (dec.op == "return") = true
              · simp only [h5, ite_true] at hFail ⊢
                unfold returnRuleProg at hFail ⊢
                match dec.rdOps, dec.wrOps, hFail with
                | [], [], hFail =>
                  dsimp only [] at hFail ⊢
                  by_cases hd1 : 1 < pi.depth
                  · simp only [hd1, ite_true, Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hFail ⊢
                    cases hvRet : T (.frameRetPc vid pi.depth) with
                    | none => rfl
                    | some vRet =>
                      rw [hvRet] at hFail
                      cases vRet <;> try rfl
                      rename_i r
                      dsimp only [] at hFail ⊢
                      cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) with
                      | false => simp
                      | true =>
                        rw [hrv] at hFail
                        simp [Prog.wr_bind, exec_write, exec] at hFail
                  · simp [hd1, Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hFail
                | _ :: _, _, _ => rfl
                | [], _ :: _, _ => rfl
              · simp only [Bool.not_eq_true] at h5
                simp only [h5, Bool.false_eq_true, ite_false] at hFail ⊢
                have hNotCore := isCoreOp_false_of_ne h1 h2 h3 h4 h5
                by_cases h6 : R.caps dec.op = true
                · simp only [h6, ite_true] at hFail ⊢
                  unfold nativeRuleProg at hFail ⊢
                  rw [exec_bind_fst, after_resolveAllRead_same] at hFail
                  rw [after_bind_op, after_resolveAllRead_same]
                  cases hrdK : (exec (resolveAllRead (I := I) (E := E) vid pi dec.rdOps) ιd T).1 with
                  | none => rfl
                  | some rdKeys =>
                    rw [hrdK] at hFail; dsimp only [] at hFail ⊢
                    rw [exec_bind_fst, after_resolveAllWrite_same] at hFail
                    rw [after_bind_op, after_resolveAllWrite_same]
                    cases hwrK : (exec (resolveAllWrite (I := I) (E := E) vid pi dec.wrOps) _ T).1 with
                    | none => rfl
                    | some wrKeys =>
                      rw [hwrK] at hFail; dsimp only [] at hFail ⊢
                      rw [exec_bind_fst, after_readKeyList_same] at hFail
                      rw [after_bind_op, after_readKeyList_same]
                      rw [exec_bind_fst, after_recvMany_same] at hFail
                      rw [after_bind_op, after_recvMany_same]
                      cases hEv : (R.native dec.op).eval wrKeys _ _ with
                      | error _ => rfl
                      | ok out =>
                        rw [hEv] at hFail; dsimp only [] at hFail
                        rw [exec_bind_fst, exec_bind_fst] at hFail
                        simp only [Prog.bind_eq, Prog.wr_bind, exec_write] at hFail
                        cases hSet : out.setStatus <;> simp [hSet] at hFail
                · simp only [Bool.not_eq_true] at h6
                  simp only [h6, Bool.false_eq_true, ite_false] at hFail ⊢
                  have hNoDelegTarget : ∀ rt, T (.delegEntry dec.op) ≠ some (.delegTarget rt) := by
                    intro rt hDel
                    exact hNoDeleg (RuleGuard.deleg hNotCore h6 ⟨rt, hDel⟩)
                  have hNoRouteHdr : ∀ rt, T (.libHeader dec.op) ≠ some (.routeHdr rt) := by
                    intro rt hHdr
                    exact hNoHandoff (RuleGuard.handoff hNotCore h6 hNoDelegTarget ⟨rt, hHdr⟩)
                  have nonDelegBranch : ∀ ιh,
                      (exec (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T).1 = .error err →
                      after (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T k = T k := by
                    intro ιh hF
                    rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hF
                    rw [Prog.bind_eq, Prog.rd_bind, after_read]
                    cases hvHdr : T (.libHeader dec.op) with
                    | none => rfl
                    | some vHdr =>
                      rw [hvHdr] at hF
                      cases vHdr <;> try rfl
                      · -- `UserCall`
                        dsimp only [] at hF ⊢
                        unfold userCallRuleProg at hF ⊢
                        rw [exec_bind_fst] at hF
                        rw [after_bind_op]
                        cases hb : (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ιh T).1 with
                        | error _ =>
                          exact (bindChild_only_writes_child (I := I) (E := E)
                            vid pi dec.op dec.rdOps dec.wrOps ιh T).after_eq hkNotChild
                        | ok u =>
                          rw [hb] at hF; cases u
                          simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hF
                      · -- `routeHdr`: impossible by `hNoRouteHdr`
                        rename_i rt
                        exact absurd hvHdr (hNoRouteHdr rt)
                  rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFail
                  rw [Prog.bind_eq, Prog.rd_bind, after_read]
                  cases hvDel : T (.delegEntry dec.op) with
                  | none => rw [hvDel] at hFail; exact nonDelegBranch _ hFail
                  | some vDel =>
                    rw [hvDel] at hFail
                    cases vDel <;> try exact nonDelegBranch _ hFail
                    rename_i rt
                    exact absurd hvDel (hNoDelegTarget rt)

/-- **Proposition 6 (`prop:failstop`, Fail-Stop without `H2` or `(P)`)**:
from any runnable store `T` with no pending stop (`T(rho) = none`) whose next
step is neither a handoff nor a delegated operation (`NotHandoffOrDeleg R vid T`),
either a rule steps (`StepOutcome.stepped`), or the step fails (`StepOutcome.failed`)
and in the post-failure store `T' = after (gatedStep R vid) ι T`:
1. `T'` moves to `error` (`T'(sigma) = some (.status .error)`),
2. `T'` records the error message (`T'(mu) = some (.errMsg err)`),
3. `kappa` is unchanged (`T'(kappa) = T(kappa)`),
4. every key outside `{sigma, mu}` and frames dead under `kappa` (including
   every live frame, `rho`, `lambda`, `/lib`, `/sys`, queue entries, and user
   data) is completely unchanged (`T' k = T k`), and
5. `StoreAgree vid T' T` holds. -/
theorem fail_stop {R : Runtime D I E} {vid : Vid}
    {T : Store Key (Val D)} (hRun : StoreRunnable vid T)
    (hRho : T (.stopKey vid) = none)
    (hNotHD : NotHandoffOrDeleg R vid T)
    (ι : Nat → I) :
    (∃ pi' evs as,
      GatedRel R vid ι T (.stepped pi' evs) as (after (gatedStep R vid) ι T)) ∨
    (∃ err as,
      let T' := after (gatedStep R vid) ι T
      GatedRel R vid ι T (.failed err) as T' ∧
      T' (.statusKey vid) = some (.status .error) ∧
      T' (.msgKey vid) = some (.errMsg err) ∧
      T' (.pcKey vid) = T (.pcKey vid) ∧
      (∀ k, k ≠ .statusKey vid → k ≠ .msgKey vid →
        isDeadFrameKey vid (liveDepthOf vid T) k = false → T' k = T k) ∧
      StoreAgree vid T' T) := by
  cases hCore : exec (stepCore R vid) ι T with
  | mk res asCore =>
    cases res with
    | ok pair =>
      rcases pair with ⟨pi', evs⟩
      left
      refine ⟨pi', evs, .read (.stopKey vid) none :: asCore, ?_⟩
      rw [gatedRel_iff_exec]
      refine ⟨hRun, ?_, rfl⟩
      simp [gatedStep, hRho, exec_bind, hCore, finalizeStep]
    | error err =>
      right
      let asFull := .read (.stopKey vid) none ::
        (asCore ++ [.write (.put (.msgKey vid) (.errMsg err)),
                    .write (.put (.statusKey vid) (.status .error))])
      have hFail : (exec (stepCore R vid) ι T).1 = .error err := by rw [hCore]
      have hExecG : exec (gatedStep R vid) ι T = (.failed err, asFull) := by
        simp [gatedStep, exec, hRho, exec_bind, hCore, finalizeStep, asFull]
      have hAfterEq : after (gatedStep R vid) ι T =
          (Write.put (.statusKey vid) (.status .error)).apply
            ((Write.put (.msgKey vid) (.errMsg err)).apply (after (stepCore R vid) ι T)) := by
        unfold gatedStep
        simp only [Prog.bind_eq, Prog.rd_bind, after_read, hRho]
        try dsimp only []
        rw [after_bind, hFail]
        dsimp only [finalizeStep]
        simp only [Prog.bind_eq, Prog.wr_bind, after_write]
        rfl
      have hRel : GatedRel R vid ι T (.failed err) asFull (after (gatedStep R vid) ι T) :=
        (gatedRel_iff_exec R vid ι T (.failed err) asFull _).2 ⟨hRun, hExecG, rfl⟩
      have hNonDead : ∀ k, k ≠ .statusKey vid → k ≠ .msgKey vid →
          isDeadFrameKey vid (liveDepthOf vid T) k = false →
          after (gatedStep R vid) ι T k = T k := by
        intro k hneSt hneMsg hnd
        rw [hAfterEq]
        simp only [Write.apply, Write.affects, decide_eq_true_eq, hneSt, hneMsg, ite_false]
        exact failed_stepCore_nonHandoff_eq hNotHD hFail hnd
      have hPcEq : after (gatedStep R vid) ι T (.pcKey vid) = T (.pcKey vid) :=
        hNonDead (.pcKey vid) (by intro h; cases h) (by intro h; cases h) rfl
      refine ⟨err, asFull, hRel, ?_, ?_, hPcEq, hNonDead, ⟨hPcEq, ?_⟩⟩
      · rw [hAfterEq]; simp [Write.apply, Write.affects, Write.newVal]
      · rw [hAfterEq]; simp [Write.apply, Write.affects, Write.newVal]
      · intro k hk
        rw [liveDepthOf_eq_of_pc_eq hPcEq] at hk
        apply hNonDead k
        · exact fun h => hk (Or.inl h)
        · exact fun h => hk (Or.inr (Or.inl h))
        · cases hdead : isDeadFrameKey vid (liveDepthOf vid T) k with
          | false => rfl
          | true => exact absurd (Or.inr (Or.inr (Or.inr (Or.inr hdead)))) hk

/-- **Proposition 6 Extended to All 8 Rules (`fail_stop_with_extensions`)**:
under `RuntimeSpec R` (`H1` and `H2`), fail-stop holds across *all* rules
including `Deleg` and `Handoff` without requiring `(P)`. -/
theorem fail_stop_with_extensions {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)} (hRun : StoreRunnable vid T)
    (hRho : T (.stopKey vid) = none) (ι : Nat → I) :
    (∃ pi' evs as,
      GatedRel R vid ι T (.stepped pi' evs) as (after (gatedStep R vid) ι T)) ∨
    (∃ err as,
      let T' := after (gatedStep R vid) ι T
      GatedRel R vid ι T (.failed err) as T' ∧
      T' (.statusKey vid) = some (.status .error) ∧
      T' (.msgKey vid) = some (.errMsg err) ∧
      T' (.pcKey vid) = T (.pcKey vid) ∧
      (∀ k, k ≠ .statusKey vid → k ≠ .msgKey vid →
        isDeadFrameKey vid (liveDepthOf vid T) k = false → T' k = T k) ∧
      StoreAgree vid T' T) := by
  cases hCore : exec (stepCore R vid) ι T with
  | mk res asCore =>
    cases res with
    | ok pair =>
      rcases pair with ⟨pi', evs⟩
      left
      refine ⟨pi', evs, .read (.stopKey vid) none :: asCore, ?_⟩
      rw [gatedRel_iff_exec]
      refine ⟨hRun, ?_, rfl⟩
      simp [gatedStep, hRho, exec_bind, hCore, finalizeStep]
    | error err =>
      right
      let asFull := .read (.stopKey vid) none ::
        (asCore ++ [.write (.put (.msgKey vid) (.errMsg err)),
                    .write (.put (.statusKey vid) (.status .error))])
      have hFail : (exec (stepCore R vid) ι T).1 = .error err := by rw [hCore]
      have hExecG : exec (gatedStep R vid) ι T = (.failed err, asFull) := by
        simp [gatedStep, exec, hRho, exec_bind, hCore, finalizeStep, asFull]
      have hAfterEq : after (gatedStep R vid) ι T =
          (Write.put (.statusKey vid) (.status .error)).apply
            ((Write.put (.msgKey vid) (.errMsg err)).apply (after (stepCore R vid) ι T)) := by
        unfold gatedStep
        simp only [Prog.bind_eq, Prog.rd_bind, after_read, hRho]
        try dsimp only []
        rw [after_bind, hFail]
        dsimp only [finalizeStep]
        simp only [Prog.bind_eq, Prog.wr_bind, after_write]
        rfl
      have hRel : GatedRel R vid ι T (.failed err) asFull (after (gatedStep R vid) ι T) :=
        (gatedRel_iff_exec R vid ι T (.failed err) asFull _).2 ⟨hRun, hExecG, rfl⟩
      have hAg : StoreAgree vid (after (gatedStep R vid) ι T) T :=
        failed_gatedStep_preserves_agree hR hRho hFail
      have hCoreRL : ∀ k, (k = .stopKey vid ∨ k = .driverKey vid) →
          after (stepCore R vid) ι T k = T k := by
        intro k hkRL
        have hkNotSt : k ≠ .statusKey vid := by rcases hkRL with rfl | rfl <;> intro h <;> cases h
        have hkNotChild : ∀ d, inFrame vid d k = false := by
          intro d; rcases hkRL with rfl | rfl <;> rfl
        have hkNotQ : ∀ op, k ≠ .queueEntry op vid := by
          intro op; rcases hkRL with rfl | rfl <;> intro h <;> cases h
        unfold stepCore at hFail ⊢
        simp only [Prog.bind_eq, Prog.rd_bind] at hFail ⊢
        rw [exec_read] at hFail; rw [after_read]
        cases hpc : T (.pcKey vid) with
        | none => rfl
        | some vPc =>
          rw [hpc] at hFail
          cases vPc <;> try rfl
          rename_i pi
          dsimp only [] at hFail ⊢
          rw [← Prog.bind_eq, exec_bind_fst, after_decodeInstr] at hFail
          rw [← Prog.bind_eq, after_bind_op, after_decodeInstr]
          cases hdec : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 with
          | error _ => rfl
          | ok dec =>
            rw [hdec] at hFail; dsimp only [] at hFail ⊢
            let ιd := shiftInputs (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T
            change (exec (dispatchInstr R vid pi dec) ιd T).1 = .error err at hFail
            change after (dispatchInstr R vid pi dec) ιd T k = T k
            by_cases hNHD : NotHandoffOrDeleg R vid T
            · -- Already covered by `failed_stepCore_nonHandoff_eq`!
              have hFull := failed_stepCore_nonHandoff_eq hNHD (by
                unfold stepCore
                simp only [Prog.bind_eq, Prog.rd_bind, exec_read, hpc]
                try dsimp only []
                rw [← Prog.bind_eq, exec_bind_fst, hdec, after_decodeInstr]
                exact hFail) (k := k) (by rcases hkRL with rfl | rfl <;> rfl)
              unfold stepCore at hFull
              simp only [Prog.bind_eq, Prog.rd_bind, after_read, hpc] at hFull
              try dsimp only [] at hFull
              rw [← Prog.bind_eq, after_bind_op, hdec, after_decodeInstr] at hFull
              exact hFull
            · -- `Deleg` or `Handoff` branch
              unfold dispatchInstr at hFail ⊢
              by_cases hc1 : (dec.op == "=") = true
              · exfalso; apply hNHD; intro p2 d2 hp2 hd2
                rw [hpc] at hp2; cases hp2
                rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T, hdec] at hd2; cases hd2
                simp only [beq_iff_eq] at hc1
                exact ⟨fun hg => by cases hg; simp [isCoreOp, hc1] at *,
                       fun hg => by cases hg; simp [isCoreOp, hc1] at *⟩
              · simp only [hc1, Bool.false_eq_true, ite_false] at hFail ⊢
                by_cases hc2 : (dec.op == "goto") = true
                · exfalso; apply hNHD; intro p2 d2 hp2 hd2
                  rw [hpc] at hp2; cases hp2
                  rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T, hdec] at hd2; cases hd2
                  simp only [beq_iff_eq] at hc2
                  exact ⟨fun hg => by cases hg; simp [isCoreOp, hc2] at *,
                         fun hg => by cases hg; simp [isCoreOp, hc2] at *⟩
                · simp only [hc2, Bool.false_eq_true, ite_false] at hFail ⊢
                  by_cases hc3 : (dec.op == "br") = true
                  · exfalso; apply hNHD; intro p2 d2 hp2 hd2
                    rw [hpc] at hp2; cases hp2
                    rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T, hdec] at hd2; cases hd2
                    simp only [beq_iff_eq] at hc3
                    exact ⟨fun hg => by cases hg; simp [isCoreOp, hc3] at *,
                           fun hg => by cases hg; simp [isCoreOp, hc3] at *⟩
                  · simp only [hc3, Bool.false_eq_true, ite_false] at hFail ⊢
                    by_cases hc4 : (dec.op == "call") = true
                    · exfalso; apply hNHD; intro p2 d2 hp2 hd2
                      rw [hpc] at hp2; cases hp2
                      rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T, hdec] at hd2; cases hd2
                      simp only [beq_iff_eq] at hc4
                      exact ⟨fun hg => by cases hg; simp [isCoreOp, hc4] at *,
                             fun hg => by cases hg; simp [isCoreOp, hc4] at *⟩
                    · simp only [hc4, Bool.false_eq_true, ite_false] at hFail ⊢
                      by_cases hc5 : (dec.op == "return") = true
                      · exfalso; apply hNHD; intro p2 d2 hp2 hd2
                        rw [hpc] at hp2; cases hp2
                        rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T, hdec] at hd2; cases hd2
                        simp only [beq_iff_eq] at hc5
                        exact ⟨fun hg => by cases hg; simp [isCoreOp, hc5] at *,
                               fun hg => by cases hg; simp [isCoreOp, hc5] at *⟩
                      · simp only [hc5, Bool.false_eq_true, ite_false] at hFail ⊢
                        by_cases hc6 : R.caps dec.op = true
                        · exfalso; apply hNHD; intro p2 d2 hp2 hd2
                          rw [hpc] at hp2; cases hp2
                          rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi ι T, hdec] at hd2; cases hd2
                          exact ⟨fun hg => by cases hg; simp [hc6] at *,
                                 fun hg => by cases hg; simp [hc6] at *⟩
                        · simp only [hc6, Bool.false_eq_true, ite_false] at hFail ⊢
                          have nonDelegRL : ∀ ιh,
                              (exec (do
                                let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                                match vHdr with
                                | some (.routeHdr _) =>
                                    handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                                | some .rwfuncHdr =>
                                    userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                                | _ => pure (.error "unmatched_opcode")) ιh T).1 = .error err →
                              after (do
                                let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                                match vHdr with
                                | some (.routeHdr _) =>
                                    handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                                | some .rwfuncHdr =>
                                    userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                                | _ => pure (.error "unmatched_opcode")) ιh T k = T k := by
                            intro ιh hF
                            rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hF
                            rw [Prog.bind_eq, Prog.rd_bind, after_read]
                            cases hvHdr : T (.libHeader dec.op) with
                            | none => rfl
                            | some vHdr =>
                              rw [hvHdr] at hF
                              cases vHdr <;> try rfl
                              · dsimp only [] at hF ⊢
                                unfold userCallRuleProg at hF ⊢
                                rw [exec_bind_fst] at hF; rw [after_bind_op]
                                cases hb : (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ιh T).1 with
                                | error _ =>
                                  exact (bindChild_only_writes_child (I := I) (E := E)
                                    vid pi dec.op dec.rdOps dec.wrOps ιh T).after_eq (hkNotChild _)
                                | ok u =>
                                  rw [hb] at hF; cases u
                                  simp [Prog.bind_eq, Prog.wr_bind, exec_write, exec] at hF
                              · dsimp only [] at hF ⊢
                                unfold handoffRuleProg at hF ⊢
                                simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hF ⊢
                                cases hq : T (.queueEntry dec.op vid) with
                                | some _ => rfl
                                | none =>
                                  rw [hq] at hF; dsimp only [] at hF ⊢
                                  simp only [Prog.wr_bind, exec_write, after_write] at hF ⊢
                                  rw [← Prog.bind_eq, exec_bind_fst] at hF
                                  rw [← Prog.bind_eq, after_bind_op, after_ext_same (hR.handoffH2 dec.op)]
                                  cases hex : (exec ((R.handoffExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                                  | error _ =>
                                    dsimp only []
                                    rw [after_write]
                                    simp [after, applyAll, Write.apply,
                                      Write.affects, hkNotQ dec.op]
                                  | ok out =>
                                    rw [hex] at hF; dsimp only [] at hF
                                    rw [← Prog.bind_eq, exec_bind_fst,
                                        ← Prog.bind_eq, exec_bind_fst] at hF
                                    simp [exec] at hF
                          rw [Prog.bind_eq, Prog.rd_bind, exec_read] at hFail
                          rw [Prog.bind_eq, Prog.rd_bind, after_read]
                          cases hvDel : T (.delegEntry dec.op) with
                          | none => rw [hvDel] at hFail; exact nonDelegRL _ hFail
                          | some vDel =>
                            rw [hvDel] at hFail
                            cases vDel <;> try exact nonDelegRL _ hFail
                            dsimp only [] at hFail ⊢
                            unfold delegRuleProg at hFail ⊢
                            simp only [Prog.bind_eq, Prog.rd_bind, exec_read, after_read] at hFail ⊢
                            cases hq : T (.queueEntry dec.op vid) with
                            | some _ => rfl
                            | none =>
                              rw [hq] at hFail; dsimp only [] at hFail ⊢
                              simp only [Prog.wr_bind, exec_write, after_write] at hFail ⊢
                              rw [← Prog.bind_eq, exec_bind_fst] at hFail
                              rw [← Prog.bind_eq, after_bind_op, after_ext_same (hR.delegH2 dec.op)]
                              cases hex : (exec ((R.delegExt dec.op).run vid pi dec.rdOps dec.wrOps) _ _).1 with
                              | error _ =>
                                dsimp only []
                                rw [after_write]
                                simp [after, applyAll, Write.apply,
                                  Write.affects, hkNotQ dec.op, hkNotSt]
                              | ok out =>
                                rw [hex] at hFail; dsimp only [] at hFail
                                rw [← Prog.bind_eq, exec_bind_fst,
                                    ← Prog.bind_eq, exec_bind_fst] at hFail
                                simp [exec] at hFail
      refine ⟨err, asFull, hRel, ?_, ?_, hAg.1, ?_, hAg⟩
      · rw [hAfterEq]; simp [Write.apply, Write.affects, Write.newVal]
      · rw [hAfterEq]; simp [Write.apply, Write.affects, Write.newVal]
      · intro k hneSt hneMsg hnd
        by_cases hkRL : k = .stopKey vid ∨ k = .driverKey vid
        · rw [hAfterEq]
          simp only [Write.apply, Write.affects, decide_eq_true_eq, hneSt, hneMsg, ite_false]
          exact hCoreRL k hkRL
        · apply hAg.2 k
          rw [liveDepthOf_eq_of_pc_eq hAg.1]
          rintro (h | h | h | h | hd)
          · exact hneSt h
          · exact hneMsg h
          · exact hkRL (Or.inl h)
          · exact hkRL (Or.inr h)
          · rw [hnd] at hd; cases hd

end FailStop

end Pathlang
