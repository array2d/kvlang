import Pathlang.Rules

/-!
# Footprint Lemma and Theorem 1 (v2): Lipton Reduction for In-Step Stops (T2)

1. Pure characterization of operand resolution (`derefChainPure`) and program
   discipline `(P)` (`StepDiscipline`).
2. **Footprint Lemma** (`gatedStep_footprint`): in every gated transition, only
   the initial gate action reads `rho` (`..stop`), and no action writes `rho` or
   `lambda` (`..driver`).
3. **Lipton Right-Mover Lemma** (`gatedContinuation_right_mover`) on live monadic
   executions where `stop = put(rho, 1)` mutates the store mid-step.
4. **Theorem 1 (v2)** (`theorem1_v2_reduction`): moving every in-step `stop` to
   the end of its step (deleting none) yields a coarse step-boundary run with
   identical trace, identical pre-gate stores, and identical final store.
5. **Corollaries** (`stop_takes_effect_at_next_gate`,
   `midstep_stop_pauses_next_gate`).
-/

namespace Pathlang

/-! ## Pure Characterization of Pointer Resolution and Discipline `(P)` -/

section PureResolve

variable {D I E : Type}

/-- Pure evaluation of `derefChain`. -/
def derefChainPure : Nat → Key → Store Key (Val D) → Option Key
  | 0, k, _ => some k
  | hops + 1, k, T =>
      match T k with
      | some (.ptr k') => derefChainPure hops k' T
      | _ => none

/-- Ordered list of keys read by `derefChain`. -/
def derefChainReads : Nat → Key → Store Key (Val D) → List Key
  | 0, _, _ => []
  | hops + 1, k, T =>
      match T k with
      | some (.ptr k') => k :: derefChainReads hops k' T
      | _ => [k]

theorem exec_derefChain (hops : Nat) (k : Key) (ι : Nat → I) (T : Store Key (Val D)) :
    exec (derefChain (D := D) (I := I) (E := E) hops k) ι T =
      (derefChainPure hops k T,
       (derefChainReads hops k T).map fun r => Act.read r (T r)) := by
  induction hops generalizing k ι with
  | zero => rfl
  | succ hops ih =>
    simp only [derefChain, derefChainPure, derefChainReads, Prog.bind_eq, Prog.rd_bind, exec_read]
    cases hk : T k with
    | none => simp [hk]
    | some v =>
      cases v <;> simp [hk, ih]

theorem after_derefChain (hops : Nat) (k : Key) (ι : Nat → I) (T : Store Key (Val D)) :
    after (derefChain (D := D) (I := I) (E := E) hops k) ι T = T := by
  unfold after
  rw [exec_derefChain]
  have hw : Act.writes ((derefChainReads hops k T).map fun r =>
      (Act.read r (T r) : Act Key (Val D) I E)) = [] := by
    induction derefChainReads hops k T with
    | nil => rfl
    | cons r rs ih => simp [ih]
  simp [hw]

def resolveReadPure (vid : Vid) (pi : PC) (opCell : Key × Val D)
    (T : Store Key (Val D)) : Option Key :=
  match opCell.2 with
  | .ref hops base => derefChainPure hops (base.startKey vid pi.depth) T
  | _ => some opCell.1

def resolveReadReads (vid : Vid) (pi : PC) (opCell : Key × Val D)
    (T : Store Key (Val D)) : List Key :=
  match opCell.2 with
  | .ref hops base => derefChainReads hops (base.startKey vid pi.depth) T
  | _ => []

def resolveWritePure (vid : Vid) (pi : PC) (opCell : Key × Val D)
    (T : Store Key (Val D)) : Option Key :=
  match opCell.2 with
  | .ref hops base => derefChainPure hops (base.startKey vid pi.depth) T
  | _ => none

def resolveWriteReads (vid : Vid) (pi : PC) (opCell : Key × Val D)
    (T : Store Key (Val D)) : List Key :=
  match opCell.2 with
  | .ref hops base => derefChainReads hops (base.startKey vid pi.depth) T
  | _ => []

theorem exec_resolveRead (vid : Vid) (pi : PC) (opCell : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    exec (resolveRead (I := I) (E := E) vid pi opCell) ι T =
      (resolveReadPure vid pi opCell T,
       (resolveReadReads vid pi opCell T).map fun r => Act.read r (T r)) := by
  rcases opCell with ⟨slotKey, cellVal⟩
  cases cellVal <;> simp [resolveRead, resolveReadPure, resolveReadReads, exec_derefChain]

theorem after_resolveRead (vid : Vid) (pi : PC) (opCell : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveRead (I := I) (E := E) vid pi opCell) ι T = T := by
  rcases opCell with ⟨slotKey, cellVal⟩
  cases cellVal <;> simp [resolveRead, after_derefChain]

theorem exec_resolveWrite (vid : Vid) (pi : PC) (opCell : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    exec (resolveWrite (I := I) (E := E) vid pi opCell) ι T =
      (resolveWritePure vid pi opCell T,
       (resolveWriteReads vid pi opCell T).map fun r => Act.read r (T r)) := by
  rcases opCell with ⟨slotKey, cellVal⟩
  cases cellVal <;> simp [resolveWrite, resolveWritePure, resolveWriteReads, exec_derefChain]

theorem after_resolveWrite (vid : Vid) (pi : PC) (opCell : Key × Val D)
    (ι : Nat → I) (T : Store Key (Val D)) :
    after (resolveWrite (I := I) (E := E) vid pi opCell) ι T = T := by
  rcases opCell with ⟨slotKey, cellVal⟩
  cases cellVal <;> simp [resolveWrite, after_derefChain]

/-- Pure operand-list reader matching `readOperandsFrom`. -/
def readOperandsPure (fn : SymName) (row : Nat) (dir : ParamDir)
    (T : Store Key (Val D)) : Nat → Nat → Option (List (Key × Val D))
  | _, 0 => some []
  | idx, count + 1 =>
      let k := Key.libOperand fn row dir idx
      match T k with
      | none => none
      | some v =>
          match readOperandsPure fn row dir T (idx + 1) count with
          | none => none
          | some rest => some ((k, v) :: rest)

theorem exec_readOperandsFrom_fst (fn : SymName) (row : Nat) (dir : ParamDir)
    (idx count : Nat) (ι : Nat → I) (T : Store Key (Val D)) :
    (exec (readOperandsFrom (D := D) (I := I) (E := E) fn row dir idx count) ι T).1 =
      readOperandsPure fn row dir T idx count := by
  induction count generalizing idx ι with
  | zero => rfl
  | succ count ih =>
    simp only [readOperandsFrom, readOperandsPure, Prog.bind_eq, Prog.rd_bind, exec_read]
    cases T (Key.libOperand fn row dir idx) with
    | none => rfl
    | some v =>
      rw [exec_bind]
      simp only [ih (idx + 1) ι]
      cases readOperandsPure fn row dir T (idx + 1) count <;> rfl

theorem after_readOperandsFrom (fn : SymName) (row : Nat) (dir : ParamDir)
    (idx count : Nat) (ι : Nat → I) (T : Store Key (Val D)) :
    after (readOperandsFrom (D := D) (I := I) (E := E) fn row dir idx count) ι T = T := by
  induction count generalizing idx ι with
  | zero => rfl
  | succ count ih =>
    simp only [readOperandsFrom, Prog.bind_eq, Prog.rd_bind, after_read]
    cases T (Key.libOperand fn row dir idx) with
    | none => rfl
    | some v =>
      rw [after_bind, ih (idx + 1) ι]
      cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn row dir (idx + 1) count) ι T).1 <;>
        rfl

/-- Pure instruction decoder matching `decodeInstr`. -/
def decodeInstrPure (vid : Vid) (pi : PC) (T : Store Key (Val D)) :
    Except ErrMsg (DecodedInstr D) :=
  if !pi.valid then
    .error "invalid_pc"
  else
    match T (.frameLib vid pi.depth) with
    | some (.sym fn) =>
        match T (.libInstr fn pi.row) with
        | some (.instr op nr nw) =>
            match readOperandsPure fn pi.row .rd T 1 nr with
            | none => .error "missing_rd_operand"
            | some rdOps =>
                match readOperandsPure fn pi.row .wr T 1 nw with
                | none => .error "missing_wr_operand"
                | some wrOps => .ok ⟨fn, op, rdOps, wrOps⟩
        | _ => .error "bad_opcode"
    | _ => .error "missing_frame_lib"

theorem exec_decodeInstr_fst (vid : Vid) (pi : PC) (ι : Nat → I) (T : Store Key (Val D)) :
    (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T).1 =
      decodeInstrPure vid pi T := by
  unfold decodeInstr decodeInstrPure
  cases hpv : pi.valid with
  | false => simp
  | true =>
    simp only [Bool.not_true, Bool.false_eq_true, ite_false, Prog.bind_eq, Prog.rd_bind]
    rw [exec_read]
    cases T (.frameLib vid pi.depth) with
    | none => rfl
    | some vLib =>
      cases vLib <;> try rfl
      rename_i fn
      simp only []
      rw [exec_read]
      cases T (.libInstr fn pi.row) with
      | none => rfl
      | some vIns =>
        cases vIns <;> try rfl
        rename_i op nr nw
        simp only []
        rw [← Prog.bind_eq, exec_bind_fst, after_readOperandsFrom, exec_readOperandsFrom_fst]
        cases readOperandsPure fn pi.row .rd T 1 nr with
        | none => rfl
        | some rdOps =>
          simp only []
          rw [← Prog.bind_eq, exec_bind_fst, exec_readOperandsFrom_fst]
          cases readOperandsPure fn pi.row .wr T 1 nw <;> rfl

theorem after_decodeInstr (vid : Vid) (pi : PC) (ι : Nat → I) (T : Store Key (Val D)) :
    after (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T = T := by
  unfold decodeInstr
  cases hpv : pi.valid with
  | false => simp
  | true =>
    simp only [Bool.not_true, Bool.false_eq_true, ite_false, Prog.bind_eq, Prog.rd_bind]
    rw [after_read]
    cases T (.frameLib vid pi.depth) with
    | none => rfl
    | some vLib =>
      cases vLib <;> try rfl
      rename_i fn
      simp only []
      rw [after_read]
      cases T (.libInstr fn pi.row) with
      | none => rfl
      | some vIns =>
        cases vIns <;> try rfl
        rename_i op nr nw
        simp only []
        rw [after_bind, after_readOperandsFrom]
        cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .rd 1 nr) ι T).1 with
        | none => rfl
        | some rdOps =>
          simp only []
          rw [after_bind, after_readOperandsFrom]
          cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .wr 1 nw) _ T).1 <;> rfl

/-- Single read-operand safety at `(vid, pi)` in store `T`. -/
def ReadOpSafe (vid : Vid) (pi : PC) (T : Store Key (Val D)) (x : Key × Val D) : Prop :=
  (∀ k ∈ resolveReadReads vid pi x T, ForbiddenKey vid pi.depth k = false) ∧
  (∀ p, resolveReadPure vid pi x T = some p → ForbiddenKey vid pi.depth p = false)

/-- Single write-operand safety at `(vid, pi)` in store `T`. -/
def WriteOpSafe (vid : Vid) (pi : PC) (T : Store Key (Val D)) (y : Key × Val D) : Prop :=
  (∀ k ∈ resolveWriteReads vid pi y T, ForbiddenKey vid pi.depth k = false) ∧
  (∀ q, resolveWritePure vid pi y T = some q →
    ForbiddenKey vid pi.depth q = false ∧ isFrameParamKey vid q = false)

/-- **Assumption `(P)` (Program Discipline, Appendix A.2 + v2 Findings 3 & 7)**:
at `T`'s current instruction for `vid`, no operand or pointer cell on its
resolution chain resolves into a dead frame, a thread control key (`kappa`,
`sigma`, `rho`, `lambda`, `mu`), a frame system key, or a queue entry of `vid`,
and no write operand resolves to a parameter binding slot `[e]/[0, +-j]`. -/
def StepDiscipline (vid : Vid) (T : Store Key (Val D)) : Prop :=
  ∀ pi dec,
    T (.pcKey vid) = some (.pc pi) →
    decodeInstrPure vid pi T = .ok dec →
    (∀ x ∈ dec.rdOps, ReadOpSafe vid pi T x) ∧
    (∀ y ∈ dec.wrOps, WriteOpSafe vid pi T y)

end PureResolve

/-! ## Modular Footprint Proofs for Rules and `gatedStep` -/

section FootprintProofs

variable {D I E : Type}

abbrev IsRho (vid : Vid) (k : Key) : Prop := k = .stopKey vid
abbrev IsRhoOrLambda (vid : Vid) (k : Key) : Prop :=
  k = .stopKey vid ∨ k = .driverKey vid

theorem not_rho_of_not_forbidden {vid : Vid} {d : Nat} {k : Key}
    (h : ForbiddenKey vid d k = false) : ¬ IsRho vid k := by
  rintro rfl
  simp [ForbiddenKey, isThreadControlKey] at h

theorem not_rho_lambda_of_not_forbidden {vid : Vid} {d : Nat} {k : Key}
    (h : ForbiddenKey vid d k = false) : ¬ IsRhoOrLambda vid k := by
  rintro (rfl | rfl) <;> simp [ForbiddenKey, isThreadControlKey] at h

theorem put_avoids_rl {vid : Vid} {k : Key} (hk : ¬ IsRhoOrLambda vid k) (v : Val D) :
    ∀ k', (Write.put k v).affects k' = true → ¬ IsRhoOrLambda vid k' := by
  intro k' haff
  simp only [Write.affects, decide_eq_true_eq] at haff
  subst haff
  exact hk

theorem del_avoids_rl {vid : Vid} {k : Key} (hk : ¬ IsRhoOrLambda vid k) :
    ∀ k', (Write.del (V := Val D) k).affects k' = true → ¬ IsRhoOrLambda vid k' := by
  intro k' haff
  simp only [Write.affects, decide_eq_true_eq] at haff
  subst haff
  exact hk

theorem delFrame_avoids_rl (vid : Vid) (d : Nat) :
    ∀ k', (Write.delWhere (V := Val D) (inFrame vid d)).affects k' = true →
      ¬ IsRhoOrLambda vid k' := by
  rintro k' haff (rfl | rfl) <;> simp [Write.affects, inFrame] at haff

theorem resolveRead_avoids {vid : Vid} {pi : PC} {x : Key × Val D}
    {T : Store Key (Val D)} (hx : ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (resolveRead (I := I) (E := E) vid pi x) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hmem
    rw [exec_resolveRead] at hmem
    simp only [List.mem_map] at hmem
    obtain ⟨r, hr, hact⟩ := hmem
    cases hact
    exact not_rho_of_not_forbidden (hx.1 k hr)
  · intro w hmem
    rw [exec_resolveRead] at hmem
    simp at hmem

theorem resolveWrite_avoids {vid : Vid} {pi : PC} {y : Key × Val D}
    {T : Store Key (Val D)} (hy : WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (resolveWrite (I := I) (E := E) vid pi y) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hmem
    rw [exec_resolveWrite] at hmem
    simp only [List.mem_map] at hmem
    obtain ⟨r, hr, hact⟩ := hmem
    cases hact
    exact not_rho_of_not_forbidden (hy.1 k hr)
  · intro w hmem
    rw [exec_resolveWrite] at hmem
    simp at hmem

theorem resolveAllRead_avoids {vid : Vid} {pi : PC}
    {xs : List (Key × Val D)} {T : Store Key (Val D)}
    (hxs : ∀ x ∈ xs, ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (resolveAllRead (I := I) (E := E) vid pi xs) ι T ∧
    after (resolveAllRead (I := I) (E := E) vid pi xs) ι T = T ∧
    (∀ ps, (exec (resolveAllRead (I := I) (E := E) vid pi xs) ι T).1 = some ps →
      ∀ p ∈ ps, ForbiddenKey vid pi.depth p = false) := by
  induction xs generalizing ι with
  | nil =>
    refine ⟨ExecAvoids.ret _ _ _, rfl, ?_⟩
    intro ps hps p hp
    cases hps
    simp at hp
  | cons x xs ih =>
    have hx := hxs x List.mem_cons_self
    have hrest := fun z hz => hxs z (List.mem_cons_of_mem _ hz)
    unfold resolveAllRead
    have hav1 := resolveRead_avoids (I := I) (E := E) hx ι
    have haf1 := after_resolveRead (I := I) (E := E) vid pi x ι T
    cases h1 : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
    | none =>
      refine ⟨ExecAvoids.bind_op hav1 (by rw [h1]; exact ExecAvoids.ret _ _ _), ?_, ?_⟩
      · rw [after_bind_op, h1, haf1]; rfl
      · intro ps hps; rw [exec_bind_fst, h1] at hps; simp at hps
    | some p =>
      have hp : ForbiddenKey vid pi.depth p = false := by
        have h1' := h1
        rw [exec_resolveRead] at h1'
        exact hx.2 p h1'
      let ι' := shiftInputs (resolveRead (I := I) (E := E) vid pi x) ι T
      obtain ⟨hav2, haf2, hps2⟩ := ih hrest ι'
      refine ⟨?_, ?_, ?_⟩
      · apply ExecAvoids.bind_op hav1
        rw [h1, haf1]
        apply ExecAvoids.bind_op hav2
        rw [haf2]
        cases (exec (resolveAllRead (I := I) (E := E) vid pi xs) ι' T).1 <;>
          exact ExecAvoids.ret _ _ _
      · rw [after_bind_op, h1, haf1, after_bind_op, haf2]
        cases (exec (resolveAllRead (I := I) (E := E) vid pi xs) ι' T).1 <;> rfl
      · intro ps hps q hq
        rw [exec_bind_fst, h1, haf1] at hps
        simp only [] at hps
        rw [exec_bind_fst] at hps
        cases h2 : (exec (resolveAllRead (I := I) (E := E) vid pi xs) ι' T).1 with
        | none =>
          dsimp only [ι'] at h2
          simp [h2] at hps
        | some ps' =>
          have h2' := h2
          dsimp only [ι'] at h2'
          simp only [h2'] at hps
          cases hps
          rcases List.mem_cons.1 hq with rfl | hq'
          · exact hp
          · exact hps2 ps' h2 q hq'

theorem resolveAllWrite_avoids {vid : Vid} {pi : PC}
    {ys : List (Key × Val D)} {T : Store Key (Val D)}
    (hys : ∀ y ∈ ys, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (resolveAllWrite (I := I) (E := E) vid pi ys) ι T ∧
    after (resolveAllWrite (I := I) (E := E) vid pi ys) ι T = T ∧
    (∀ qs, (exec (resolveAllWrite (I := I) (E := E) vid pi ys) ι T).1 = some qs →
      ∀ q ∈ qs, ForbiddenKey vid pi.depth q = false ∧ isFrameParamKey vid q = false) := by
  induction ys generalizing ι with
  | nil =>
    refine ⟨ExecAvoids.ret _ _ _, rfl, ?_⟩
    intro qs hqs q hq
    cases hqs
    simp at hq
  | cons y ys ih =>
    have hy := hys y List.mem_cons_self
    have hrest := fun z hz => hys z (List.mem_cons_of_mem _ hz)
    unfold resolveAllWrite
    have hav1 := resolveWrite_avoids (I := I) (E := E) hy ι
    have haf1 := after_resolveWrite (I := I) (E := E) vid pi y ι T
    cases h1 : (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
    | none =>
      refine ⟨ExecAvoids.bind_op hav1 (by rw [h1]; exact ExecAvoids.ret _ _ _), ?_, ?_⟩
      · rw [after_bind_op, h1, haf1]; rfl
      · intro qs hqs; rw [exec_bind_fst, h1] at hqs; simp at hqs
    | some q =>
      have hqSafe : ForbiddenKey vid pi.depth q = false ∧ isFrameParamKey vid q = false := by
        have h1' := h1
        rw [exec_resolveWrite] at h1'
        exact hy.2 q h1'
      let ι' := shiftInputs (resolveWrite (I := I) (E := E) vid pi y) ι T
      obtain ⟨hav2, haf2, hqs2⟩ := ih hrest ι'
      refine ⟨?_, ?_, ?_⟩
      · apply ExecAvoids.bind_op hav1
        rw [h1, haf1]
        apply ExecAvoids.bind_op hav2
        rw [haf2]
        cases (exec (resolveAllWrite (I := I) (E := E) vid pi ys) ι' T).1 <;>
          exact ExecAvoids.ret _ _ _
      · rw [after_bind_op, h1, haf1, after_bind_op, haf2]
        cases (exec (resolveAllWrite (I := I) (E := E) vid pi ys) ι' T).1 <;> rfl
      · intro qs hqs q' hq'
        rw [exec_bind_fst, h1, haf1] at hqs
        simp only [] at hqs
        rw [exec_bind_fst] at hqs
        cases h2 : (exec (resolveAllWrite (I := I) (E := E) vid pi ys) ι' T).1 with
        | none =>
          dsimp only [ι'] at h2
          simp [h2] at hqs
        | some qs' =>
          have h2' := h2
          dsimp only [ι'] at h2'
          simp only [h2'] at hqs
          cases hqs
          rcases List.mem_cons.1 hq' with rfl | hq''
          · exact hqSafe
          · exact hqs2 qs' h2 q' hq''

theorem readOperandsFrom_avoids (vid : Vid) (fn : SymName)
    (row : Nat) (dir : ParamDir) (idx count : Nat) (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (readOperandsFrom (D := D) (I := I) (E := E) fn row dir idx count) ι T := by
  induction count generalizing idx ι with
  | zero => exact ExecAvoids.ret _ _ _
  | succ count ih =>
    unfold readOperandsFrom
    apply ExecAvoids.rd_bind (by simp [IsRho])
    cases T (.libOperand fn row dir idx) with
    | none => exact ExecAvoids.ret _ _ _
    | some v =>
      apply ExecAvoids.bind_op (ih (idx + 1) _)
      cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn row dir (idx + 1) count) _ _).1 <;>
        exact ExecAvoids.ret _ _ _

theorem decodeInstr_avoids (vid : Vid) (pi : PC)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T := by
  unfold decodeInstr
  cases hpv : pi.valid with
  | false =>
    simp only [Bool.not_false, ite_true]
    exact ExecAvoids.ret _ _ _
  | true =>
    simp only [Bool.not_true, Bool.false_eq_true, ite_false]
    apply ExecAvoids.rd_bind (by simp [IsRho])
    cases T (.frameLib vid pi.depth) with
    | none => exact ExecAvoids.ret _ _ _
    | some vLib =>
      cases vLib <;> try exact ExecAvoids.ret _ _ _
      rename_i fn
      apply ExecAvoids.rd_bind (by simp [IsRho])
      cases T (.libInstr fn pi.row) with
      | none => exact ExecAvoids.ret _ _ _
      | some vIns =>
        cases vIns <;> try exact ExecAvoids.ret _ _ _
        rename_i op nr nw
        apply ExecAvoids.bind_op (readOperandsFrom_avoids vid fn pi.row .rd 1 nr _ _)
        cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .rd 1 nr) _ _).1 with
        | none => exact ExecAvoids.ret _ _ _
        | some rdOps =>
          apply ExecAvoids.bind_op (readOperandsFrom_avoids vid fn pi.row .wr 1 nw _ _)
          cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .wr 1 nw) _ _).1 <;>
            exact ExecAvoids.ret _ _ _

theorem resolveRowTarget_avoids {vid : Vid} {pi : PC} {x : Key × Val D}
    {T : Store Key (Val D)} (hx : ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (resolveRowTarget (I := I) (E := E) vid pi x) ι T := by
  unfold resolveRowTarget
  apply ExecAvoids.bind_op (resolveRead_avoids hx ι)
  rw [after_resolveRead]
  cases hp : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none => exact ExecAvoids.ret _ _ _
  | some p =>
    have hp' := hp
    rw [exec_resolveRead] at hp'
    apply ExecAvoids.rd_bind (not_rho_of_not_forbidden (hx.2 p hp'))
    cases T p with
    | none => exact ExecAvoids.ret _ _ _
    | some v =>
      cases v <;> try exact ExecAvoids.ret _ _ _
      rename_i z
      cases z with
      | ofNat m => cases m <;> exact ExecAvoids.ret _ _ _
      | negSucc _ => exact ExecAvoids.ret _ _ _

theorem resolveTruth_avoids_and_after {vid : Vid} {pi : PC} {x : Key × Val D}
    {T : Store Key (Val D)} (hx : ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (resolveTruth (I := I) (E := E) vid pi x) ι T ∧
    after (resolveTruth (I := I) (E := E) vid pi x) ι T = T := by
  unfold resolveTruth
  have hav1 := resolveRead_avoids (I := I) (E := E) hx ι
  have haf1 := after_resolveRead (I := I) (E := E) vid pi x ι T
  cases hp : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none =>
    refine ⟨ExecAvoids.bind_op hav1 (by rw [hp]; exact ExecAvoids.ret _ _ _), ?_⟩
    rw [after_bind_op, hp, haf1]; rfl
  | some p =>
    have hp' := hp
    rw [exec_resolveRead] at hp'
    refine ⟨?_, ?_⟩
    · apply ExecAvoids.bind_op hav1
      rw [hp, haf1]
      apply ExecAvoids.rd_bind (not_rho_of_not_forbidden (hx.2 p hp'))
      cases T p with
      | none => exact ExecAvoids.ret _ _ _
      | some v => cases v <;> exact ExecAvoids.ret _ _ _
    · rw [after_bind_op, hp, haf1]
      simp only [Prog.bind_eq, Prog.rd_bind, after_read]
      cases T p with
      | none => rfl
      | some v => cases v <;> rfl

theorem derefChain_eq_of_child_mod {vid : Vid} {d : Nat} {hops : Nat} {k : Key}
    {T T' : Store Key (Val D)}
    (hDiff : ∀ r, ForbiddenKey vid d r = false → T' r = T r)
    (hSafe : ∀ r ∈ derefChainReads hops k T, ForbiddenKey vid d r = false) :
    derefChainPure hops k T' = derefChainPure hops k T ∧
    derefChainReads hops k T' = derefChainReads hops k T := by
  induction hops generalizing k with
  | zero => exact ⟨rfl, rfl⟩
  | succ hops ih =>
    simp only [derefChainPure, derefChainReads] at hSafe ⊢
    have hkSafe : ForbiddenKey vid d k = false := by
      cases hk : T k with
      | none => simp [hk] at hSafe; exact hSafe
      | some v =>
        cases v <;> (simp [hk] at hSafe; (try exact hSafe); (try exact hSafe.1))
    rw [hDiff k hkSafe]
    cases hk : T k with
    | none => exact ⟨rfl, rfl⟩
    | some v =>
      cases v <;> try exact ⟨rfl, rfl⟩
      rename_i k'
      simp [hk] at hSafe
      obtain ⟨ih1, ih2⟩ := ih hSafe.2
      exact ⟨ih1, congrArg (k :: ·) ih2⟩

theorem readOpSafe_of_agree_nonforbidden {vid : Vid} {pi : PC} {x : Key × Val D}
    {T T' : Store Key (Val D)}
    (hDiff : ∀ r, ForbiddenKey vid pi.depth r = false → T' r = T r)
    (hx : ReadOpSafe vid pi T x) : ReadOpSafe vid pi T' x := by
  rcases x with ⟨slotKey, cellVal⟩
  cases cellVal <;> try exact hx
  rename_i hops base
  simp only [ReadOpSafe, resolveReadReads, resolveReadPure] at hx ⊢
  obtain ⟨hpure, hreads⟩ := derefChain_eq_of_child_mod hDiff hx.1
  rw [hpure, hreads]
  exact hx

theorem writeOpSafe_of_agree_nonforbidden {vid : Vid} {pi : PC} {y : Key × Val D}
    {T T' : Store Key (Val D)}
    (hDiff : ∀ r, ForbiddenKey vid pi.depth r = false → T' r = T r)
    (hy : WriteOpSafe vid pi T y) : WriteOpSafe vid pi T' y := by
  rcases y with ⟨slotKey, cellVal⟩
  cases cellVal <;> try exact hy
  rename_i hops base
  simp only [WriteOpSafe, resolveWriteReads, resolveWritePure] at hy ⊢
  obtain ⟨hpure, hreads⟩ := derefChain_eq_of_child_mod hDiff hy.1
  rw [hpure, hreads]
  exact hy

theorem write_child_preserves_nonforbidden {vid : Vid} {d : Nat} {w : Write Key (Val D)}
    {T : Store Key (Val D)}
    (hw : ∀ k, w.affects k = true → inFrame vid (d + 1) k = true) :
    ∀ r, ForbiddenKey vid d r = false → w.apply T r = T r := by
  intro r hr
  apply Write.apply_of_not_affects
  cases haff : w.affects r with
  | false => rfl
  | true =>
    have hdead := inFrame_iff_dead_of_gt (by omega : d < d + 1) (hw r haff)
    simp [ForbiddenKey, hdead] at hr

theorem bindReadArgs_avoids {vid : Vid} {pi : PC} (idx : Nat)
    (xs : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D))
    (hxs : ∀ x ∈ xs, ReadOpSafe vid pi T x) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (bindReadArgs (I := I) (E := E) vid pi idx xs) ι T ∧
    (∀ r, ForbiddenKey vid pi.depth r = false →
      after (bindReadArgs (I := I) (E := E) vid pi idx xs) ι T r = T r) := by
  induction xs generalizing idx ι T with
  | nil => exact ⟨ExecAvoids.ret _ _ _, fun _ _ => rfl⟩
  | cons x xs ih =>
    have hx := hxs x List.mem_cons_self
    unfold bindReadArgs
    have hav1 := resolveRead_avoids (I := I) (E := E) hx ι
    have haf1 := after_resolveRead (I := I) (E := E) vid pi x ι T
    cases h1 : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
    | none =>
      refine ⟨ExecAvoids.bind_op hav1 (by rw [h1]; exact ExecAvoids.ret _ _ _), ?_⟩
      intro r _; rw [after_bind_op, h1, haf1]; rfl
    | some p =>
      let w := Write.put (K := Key) (V := Val D) (.frameParam vid (pi.depth + 1) .rd idx) (.ptr p)
      have hwNotRL := put_avoids_rl (vid := vid)
        (k := .frameParam vid (pi.depth + 1) .rd idx)
        (by rintro (h | h) <;> cases h) (.ptr p : Val D)
      have hwChild : ∀ k, w.affects k = true → inFrame vid (pi.depth + 1) k = true := by
        intro k hk; simp only [w, Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame]
      have hT' := write_child_preserves_nonforbidden (T := T) hwChild
      have hxs' : ∀ z ∈ xs, ReadOpSafe vid pi (w.apply T) z :=
        fun z hz => readOpSafe_of_agree_nonforbidden hT' (hxs z (List.mem_cons_of_mem _ hz))
      let ι' := shiftInputs (resolveRead (I := I) (E := E) vid pi x) ι T
      obtain ⟨havRest, hafRest⟩ := ih (idx + 1) ι' (w.apply T) hxs'
      refine ⟨?_, ?_⟩
      · apply ExecAvoids.bind_op hav1
        rw [h1, haf1]
        exact ExecAvoids.wr_bind hwNotRL havRest
      · intro r hr
        rw [after_bind_op, h1, haf1]
        simp only [Prog.bind_eq, Prog.wr_bind, after_write]
        exact (hafRest r hr).trans (hT' r hr)

theorem bindWriteArgs_avoids {vid : Vid} {pi : PC} (idx : Nat)
    (ys : List (Key × Val D)) (ι : Nat → I) (T : Store Key (Val D))
    (hys : ∀ y ∈ ys, WriteOpSafe vid pi T y) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (bindWriteArgs (I := I) (E := E) vid pi idx ys) ι T := by
  induction ys generalizing idx ι T with
  | nil => exact ExecAvoids.ret _ _ _
  | cons y ys ih =>
    have hy := hys y List.mem_cons_self
    unfold bindWriteArgs
    have hav1 := resolveWrite_avoids (I := I) (E := E) hy ι
    have haf1 := after_resolveWrite (I := I) (E := E) vid pi y ι T
    cases h1 : (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
    | none => exact ExecAvoids.bind_op hav1 (by rw [h1]; exact ExecAvoids.ret _ _ _)
    | some q =>
      let w := Write.put (K := Key) (V := Val D) (.frameParam vid (pi.depth + 1) .wr idx) (.ptr q)
      have hwNotRL := put_avoids_rl (vid := vid)
        (k := .frameParam vid (pi.depth + 1) .wr idx)
        (by rintro (h | h) <;> cases h) (.ptr q : Val D)
      have hwChild : ∀ k, w.affects k = true → inFrame vid (pi.depth + 1) k = true := by
        intro k hk; simp only [w, Write.affects, decide_eq_true_eq] at hk; subst hk; simp [inFrame]
      have hT' := write_child_preserves_nonforbidden (T := T) hwChild
      have hys' : ∀ z ∈ ys, WriteOpSafe vid pi (w.apply T) z :=
        fun z hz => writeOpSafe_of_agree_nonforbidden hT' (hys z (List.mem_cons_of_mem _ hz))
      apply ExecAvoids.bind_op hav1
      rw [h1, haf1]
      exact ExecAvoids.wr_bind hwNotRL (ih (idx + 1) _ (w.apply T) hys')

theorem bindChild_avoids {vid : Vid} {pi : PC} {callee : SymName}
    {rdArgs wrArgs : List (Key × Val D)} {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdArgs, ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrArgs, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T := by
  unfold bindChild
  apply ExecAvoids.rd_bind (by simp [IsRho])
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
      apply ExecAvoids.wr_bind (delFrame_avoids_rl vid dChild)
      apply ExecAvoids.wr_bind (put_avoids_rl (by rintro (h | h) <;> cases h) (.sym callee))
      apply ExecAvoids.wr_bind (put_avoids_rl (by rintro (h | h) <;> cases h) (.pc pi.next))
      apply ExecAvoids.wr_bind (put_avoids_rl (by rintro (h | h) <;> cases h) (.pc pi.entry))
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
      obtain ⟨havRd, hafRd⟩ := bindReadArgs_avoids (I := I) (E := E) 1 rdArgs ι T₄ hrd₄
      apply ExecAvoids.bind_op havRd
      cases (exec (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) ι T₄).1 with
      | error _ => exact ExecAvoids.ret _ _ _
      | ok _ =>
        have hwr₅ : ∀ y ∈ wrArgs, WriteOpSafe vid pi
            (after (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) ι T₄) y :=
          fun y hy => writeOpSafe_of_agree_nonforbidden
            (fun r hr => (hafRd r hr).trans (hT₄ r hr)) (hwr y hy)
        exact bindWriteArgs_avoids 1 wrArgs _ _ hwr₅

theorem readKeyList_avoids {vid : Vid} {pi : PC} {ks : List Key}
    (hks : ∀ k ∈ ks, ForbiddenKey vid pi.depth k = false)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (readKeyList (D := D) (I := I) (E := E) ks) ι T ∧
    after (readKeyList (D := D) (I := I) (E := E) ks) ι T = T := by
  induction ks generalizing ι with
  | nil => exact ⟨ExecAvoids.ret _ _ _, rfl⟩
  | cons k ks ih =>
    unfold readKeyList
    have hk := not_rho_of_not_forbidden (hks k List.mem_cons_self)
    obtain ⟨havRest, hafRest⟩ := ih (fun k' hk' => hks k' (List.mem_cons_of_mem _ hk')) ι
    refine ⟨?_, ?_⟩
    · apply ExecAvoids.rd_bind hk
      apply ExecAvoids.bind_op havRest
      exact ExecAvoids.ret _ _ _
    · simp only [Prog.bind_eq, Prog.rd_bind, after_read]
      rw [after_bind, hafRest]; rfl

theorem recvMany_avoids (vid : Vid) (n : Nat)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (recvMany (D := D) (I := I) (E := E) n) ι T := by
  induction n generalizing ι with
  | zero => exact ExecAvoids.ret _ _ _
  | succ n ih =>
    unfold recvMany
    apply ExecAvoids.recv_bind
    exact ExecAvoids.bind_op (ih (fun m => ι (m + 1))) (ExecAvoids.ret _ _ _)

theorem emitAll_avoids (vid : Vid) (evs : List E)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (emitAll (D := D) (I := I) evs) ι T := by
  induction evs generalizing ι with
  | nil => exact ExecAvoids.ret _ _ _
  | cons e evs ih =>
    unfold emitAll
    exact ExecAvoids.out_bind (ih ι)

theorem putAll_avoids {vid : Vid} {pi : PC} {kvs : List (Key × Val D)}
    (hkvs : ∀ k v, (k, v) ∈ kvs → ForbiddenKey vid pi.depth k = false)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (putAll (I := I) (E := E) kvs) ι T := by
  induction kvs generalizing ι T with
  | nil => exact ExecAvoids.ret _ _ _
  | cons kv kvs ih =>
    rcases kv with ⟨k, v⟩
    unfold putAll
    have hk := not_rho_lambda_of_not_forbidden (hkvs k v List.mem_cons_self)
    exact ExecAvoids.wr_bind (put_avoids_rl hk v)
      (ih (fun k' v' hmem => hkvs k' v' (List.mem_cons_of_mem _ hmem)) ι _)

theorem not_rl_pc (vid : Vid) (pi' : PC) :
    ∀ k, (Write.put (V := Val D) (.pcKey vid) (.pc pi')).affects k = true →
      ¬ IsRhoOrLambda vid k :=
  put_avoids_rl (by rintro (h | h) <;> cases h) (.pc pi')

theorem not_rl_status (vid : Vid) (s : Status) :
    ∀ k, (Write.put (V := Val D) (.statusKey vid) (.status s)).affects k = true →
      ¬ IsRhoOrLambda vid k :=
  put_avoids_rl (by rintro (h | h) <;> cases h) (.status s)

theorem ext_avoids {ext : Extension D I E} (hExt : ExtensionSpec ext)
    (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D))
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (ext.run vid pi rdOps wrOps) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hmem
    exact not_rho_of_not_forbidden (hExt.readsSafe vid pi rdOps wrOps ι T k v hmem)
  · intro w hmem k _
    have hwNil := (hExt.internalPure vid pi rdOps wrOps ι T).1
    have hwIn := Act.mem_writes_of_mem hmem
    rw [hwNil] at hwIn
    simp at hwIn

theorem copyRule_avoids {vid : Vid} {pi : PC} {rdOps wrOps : List (Key × Val D)}
    {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdOps, ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrOps, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (copyRuleProg (I := I) (E := E) vid pi rdOps wrOps) ι T := by
  unfold copyRuleProg
  match rdOps, wrOps with
  | [rd1], [wr1] =>
    have hr1 := hrd rd1 List.mem_cons_self
    have hw1 := hwr wr1 List.mem_cons_self
    apply ExecAvoids.bind_op (resolveRead_avoids hr1 ι)
    rw [after_resolveRead]
    cases hp : (exec (resolveRead (I := I) (E := E) vid pi rd1) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some p =>
      have hp' := hp; rw [exec_resolveRead] at hp'
      apply ExecAvoids.bind_op (resolveWrite_avoids hw1 _)
      rw [after_resolveWrite]
      cases hq : (exec (resolveWrite (I := I) (E := E) vid pi wr1) _ T).1 with
      | none => exact ExecAvoids.ret _ _ _
      | some q =>
        have hq' := hq; rw [exec_resolveWrite] at hq'
        apply ExecAvoids.rd_bind (not_rho_of_not_forbidden (hr1.2 p hp'))
        cases T p with
        | none => exact ExecAvoids.ret _ _ _
        | some val =>
          have hqRL := put_avoids_rl (not_rho_lambda_of_not_forbidden (hw1.2 q hq').1) val
          apply ExecAvoids.wr_bind hqRL
          apply ExecAvoids.wr_bind (not_rl_pc vid pi.next)
          exact ExecAvoids.ret _ _ _
  | [], _ => exact ExecAvoids.ret _ _ _
  | _ :: _ :: _, _ => exact ExecAvoids.ret _ _ _
  | [_], [] => exact ExecAvoids.ret _ _ _
  | [_], _ :: _ :: _ => exact ExecAvoids.ret _ _ _

theorem gotoRule_avoids {vid : Vid} {pi : PC} {rdOps wrOps : List (Key × Val D)}
    {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdOps, ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (gotoRuleProg (I := I) (E := E) vid pi rdOps wrOps) ι T := by
  unfold gotoRuleProg
  match rdOps, wrOps with
  | [rd1], [] =>
    have hr1 := hrd rd1 List.mem_cons_self
    apply ExecAvoids.bind_op (resolveRowTarget_avoids hr1 ι)
    cases (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some n =>
      exact ExecAvoids.wr_bind (not_rl_pc vid (pi.target n)) (ExecAvoids.ret _ _ _)
  | [], _ => exact ExecAvoids.ret _ _ _
  | _ :: _ :: _, _ => exact ExecAvoids.ret _ _ _
  | [_], _ :: _ => exact ExecAvoids.ret _ _ _

theorem brRule_avoids {vid : Vid} {pi : PC} {rdOps wrOps : List (Key × Val D)}
    {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdOps, ReadOpSafe vid pi T x) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (brRuleProg (I := I) (E := E) vid pi rdOps wrOps) ι T := by
  unfold brRuleProg
  match rdOps, wrOps with
  | [rd1, rd2, rd3], [] =>
    have hr1 := hrd rd1 (by simp)
    have hr2 := hrd rd2 (by simp)
    have hr3 := hrd rd3 (by simp)
    obtain ⟨havT, hafT⟩ := resolveTruth_avoids_and_after (I := I) (E := E) hr1 ι
    apply ExecAvoids.bind_op havT
    rw [hafT]
    cases (exec (resolveTruth (I := I) (E := E) vid pi rd1) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some b =>
      have hrb : ReadOpSafe vid pi T (if b then rd2 else rd3) := by cases b <;> assumption
      apply ExecAvoids.bind_op (resolveRowTarget_avoids hrb _)
      cases (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) _ T).1 with
      | none => exact ExecAvoids.ret _ _ _
      | some n =>
        exact ExecAvoids.wr_bind (not_rl_pc vid (pi.target n)) (ExecAvoids.ret _ _ _)
  | [], _ => exact ExecAvoids.ret _ _ _
  | [_], _ => exact ExecAvoids.ret _ _ _
  | [_, _], _ => exact ExecAvoids.ret _ _ _
  | _ :: _ :: _ :: _ :: _, _ => exact ExecAvoids.ret _ _ _
  | [_, _, _], _ :: _ => exact ExecAvoids.ret _ _ _

theorem callRule_avoids {vid : Vid} {pi : PC} {rdOps wrOps : List (Key × Val D)}
    {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdOps, ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrOps, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (callRuleProg (I := I) (E := E) vid pi rdOps wrOps) ι T := by
  unfold callRuleProg
  match rdOps with
  | [] => exact ExecAvoids.ret _ _ _
  | fnOp :: argRdOps =>
    have hfn := hrd fnOp List.mem_cons_self
    have hargs := fun x hx => hrd x (List.mem_cons_of_mem _ hx)
    apply ExecAvoids.bind_op (resolveRead_avoids hfn ι)
    rw [after_resolveRead]
    cases hp : (exec (resolveRead (I := I) (E := E) vid pi fnOp) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some pFn =>
      have hp' := hp; rw [exec_resolveRead] at hp'
      apply ExecAvoids.rd_bind (not_rho_of_not_forbidden (hfn.2 pFn hp'))
      cases T pFn with
      | none => exact ExecAvoids.ret _ _ _
      | some vFn =>
        cases vFn <;> try exact ExecAvoids.ret _ _ _
        rename_i callee
        apply ExecAvoids.bind_op (bindChild_avoids hargs hwr _)
        cases (exec (bindChild (I := I) (E := E) vid pi callee argRdOps wrOps) _ T).1 with
        | error _ => exact ExecAvoids.ret _ _ _
        | ok _ =>
          exact ExecAvoids.wr_bind (not_rl_pc vid pi.entry) (ExecAvoids.ret _ _ _)

theorem returnRule_avoids (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D))
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (returnRuleProg (I := I) (E := E) vid pi rdOps wrOps) ι T := by
  unfold returnRuleProg
  match rdOps, wrOps with
  | [], [] =>
    by_cases hd : 1 < pi.depth
    · simp only [hd, ite_true]
      apply ExecAvoids.rd_bind (by simp [IsRho])
      cases T (.frameRetPc vid pi.depth) with
      | none => exact ExecAvoids.ret _ _ _
      | some vRet =>
        cases vRet <;> try exact ExecAvoids.ret _ _ _
        rename_i r
        cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) with
        | true =>
          simp only [hrv, ite_true]
          apply ExecAvoids.wr_bind (not_rl_pc vid r)
          apply ExecAvoids.wr_bind (delFrame_avoids_rl vid pi.depth)
          exact ExecAvoids.ret _ _ _
        | false =>
          simp only [hrv, Bool.false_eq_true, ite_false]
          exact ExecAvoids.ret _ _ _
    · simp only [hd, ite_false]
      exact ExecAvoids.wr_bind (not_rl_status vid .ok) (ExecAvoids.ret _ _ _)
  | _ :: _, _ => exact ExecAvoids.ret _ _ _
  | [], _ :: _ => exact ExecAvoids.ret _ _ _

theorem nativeRule_avoids {h : NativeHandler D I E} (hH1 : HandlerSpec h)
    {vid : Vid} {pi : PC} {rdOps wrOps : List (Key × Val D)} {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdOps, ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrOps, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (nativeRuleProg h vid pi rdOps wrOps) ι T := by
  unfold nativeRuleProg
  obtain ⟨havRd, hafRd, hpsRd⟩ := resolveAllRead_avoids (I := I) (E := E) hrd ι
  apply ExecAvoids.bind_op havRd
  rw [hafRd]
  cases hrdK : (exec (resolveAllRead (I := I) (E := E) vid pi rdOps) ι T).1 with
  | none => exact ExecAvoids.ret _ _ _
  | some rdKeys =>
    let ι₁ := shiftInputs (resolveAllRead (I := I) (E := E) vid pi rdOps) ι T
    obtain ⟨havWr, hafWr, hqsWr⟩ := resolveAllWrite_avoids (I := I) (E := E) hwr ι₁
    apply ExecAvoids.bind_op havWr
    rw [hafWr]
    cases hwrK : (exec (resolveAllWrite (I := I) (E := E) vid pi wrOps) ι₁ T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some wrKeys =>
      obtain ⟨havVals, hafVals⟩ := readKeyList_avoids (I := I) (E := E) (hpsRd rdKeys hrdK) _ T
      apply ExecAvoids.bind_op havVals
      rw [hafVals]
      apply ExecAvoids.bind_op (recvMany_avoids vid h.numInputs _ T)
      cases hEval : h.eval wrKeys _ _ with
      | error _ => exact ExecAvoids.ret _ _ _
      | ok out =>
        have hSub := (hH1 wrKeys _ _ out hEval).1
        have hOutWr : ∀ k v, (k, v) ∈ out.writes → ForbiddenKey vid pi.depth k = false :=
          fun k v hkv => (hqsWr wrKeys hwrK k (hSub k v hkv)).1
        apply ExecAvoids.bind_op (emitAll_avoids vid out.events _ _)
        apply ExecAvoids.bind_op (putAll_avoids hOutWr _ _)
        apply ExecAvoids.wr_bind (not_rl_pc vid pi.next)
        cases out.setStatus with
        | none => exact ExecAvoids.ret _ _ _
        | some s =>
          exact ExecAvoids.wr_bind (not_rl_status vid s) (ExecAvoids.ret _ _ _)

theorem delegRule_avoids {ext : Extension D I E} (hExt : ExtensionSpec ext)
    (op : SymName) (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D))
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (delegRuleProg ext op vid pi rdOps wrOps) ι T := by
  unfold delegRuleProg
  have hQueueNotRL := put_avoids_rl (vid := vid) (k := .queueEntry op vid)
    (by rintro (h | h) <;> cases h) (.pc pi : Val D)
  have hDelQNotRL := del_avoids_rl (D := D) (vid := vid) (k := .queueEntry op vid)
    (by rintro (h | h) <;> cases h)
  apply ExecAvoids.rd_bind (by simp [IsRho])
  cases T (.queueEntry op vid) with
  | some _ => exact ExecAvoids.ret _ _ _
  | none =>
    apply ExecAvoids.wr_bind (not_rl_status vid .wait)
    apply ExecAvoids.wr_bind hQueueNotRL
    apply ExecAvoids.bind_op (ext_avoids hExt vid pi rdOps wrOps _ _)
    cases hEx : (exec (ext.run vid pi rdOps wrOps) _ _).1 with
    | error _ =>
      exact ExecAvoids.wr_bind hDelQNotRL (ExecAvoids.ret _ _ _)
    | ok out =>
      have hOutWr : ∀ k v, (k, v) ∈ out.writes → ForbiddenKey vid pi.depth k = false :=
        fun k v hkv => (hExt.writesSafe vid pi rdOps wrOps _ _ out k v hEx hkv).1
      apply ExecAvoids.bind_op (emitAll_avoids vid out.events _ _)
      apply ExecAvoids.bind_op (putAll_avoids hOutWr _ _)
      apply ExecAvoids.wr_bind (not_rl_status vid .running)
      apply ExecAvoids.wr_bind (not_rl_pc vid out.nextPc)
      exact ExecAvoids.wr_bind hDelQNotRL (ExecAvoids.ret _ _ _)

theorem handoffRule_avoids {ext : Extension D I E} (hExt : ExtensionSpec ext)
    (op : SymName) (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D))
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (handoffRuleProg ext op vid pi rdOps wrOps) ι T := by
  unfold handoffRuleProg
  have hQueueNotRL := put_avoids_rl (vid := vid) (k := .queueEntry op vid)
    (by rintro (h | h) <;> cases h) (.pc pi : Val D)
  have hDelQNotRL := del_avoids_rl (D := D) (vid := vid) (k := .queueEntry op vid)
    (by rintro (h | h) <;> cases h)
  apply ExecAvoids.rd_bind (by simp [IsRho])
  cases T (.queueEntry op vid) with
  | some _ => exact ExecAvoids.ret _ _ _
  | none =>
    apply ExecAvoids.wr_bind hQueueNotRL
    apply ExecAvoids.bind_op (ext_avoids hExt vid pi rdOps wrOps _ _)
    cases hEx : (exec (ext.run vid pi rdOps wrOps) _ _).1 with
    | error _ =>
      exact ExecAvoids.wr_bind hDelQNotRL (ExecAvoids.ret _ _ _)
    | ok out =>
      have hOutWr : ∀ k v, (k, v) ∈ out.writes → ForbiddenKey vid pi.depth k = false :=
        fun k v hkv => (hExt.writesSafe vid pi rdOps wrOps _ _ out k v hEx hkv).1
      apply ExecAvoids.bind_op (emitAll_avoids vid out.events _ _)
      apply ExecAvoids.bind_op (putAll_avoids hOutWr _ _)
      apply ExecAvoids.wr_bind (not_rl_pc vid out.nextPc)
      exact ExecAvoids.wr_bind hDelQNotRL (ExecAvoids.ret _ _ _)

theorem userCallRule_avoids (op : SymName) {vid : Vid} {pi : PC}
    {rdOps wrOps : List (Key × Val D)} {T : Store Key (Val D)}
    (hrd : ∀ x ∈ rdOps, ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrOps, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (userCallRuleProg (I := I) (E := E) op vid pi rdOps wrOps) ι T := by
  unfold userCallRuleProg
  apply ExecAvoids.bind_op (bindChild_avoids hrd hwr ι)
  cases (exec (bindChild (I := I) (E := E) vid pi op rdOps wrOps) ι T).1 with
  | error _ => exact ExecAvoids.ret _ _ _
  | ok _ => exact ExecAvoids.wr_bind (not_rl_pc vid pi.entry) (ExecAvoids.ret _ _ _)

/-- Under `RuntimeSpec R` and `StepDiscipline vid T`, no action of `stepCore`
reads `rho` or writes `rho` or `lambda`. -/
theorem stepCore_avoids_rho_lambda {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)} (hP : StepDiscipline vid T) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid) (stepCore R vid) ι T := by
  unfold stepCore
  apply ExecAvoids.rd_bind (by simp [IsRho])
  cases hpc : T (.pcKey vid) with
  | none => exact ExecAvoids.ret _ _ _
  | some vPc =>
    cases vPc <;> try exact ExecAvoids.ret _ _ _
    rename_i pi
    apply ExecAvoids.bind_op (decodeInstr_avoids vid pi ι T)
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
      · simp only [h1, ite_true]; exact copyRule_avoids hrd hwr _
      · simp only [h1, Bool.false_eq_true, ite_false]
        by_cases h2 : (dec.op == "goto") = true
        · simp only [h2, ite_true]; exact gotoRule_avoids hrd _
        · simp only [h2, Bool.false_eq_true, ite_false]
          by_cases h3 : (dec.op == "br") = true
          · simp only [h3, ite_true]; exact brRule_avoids hrd _
          · simp only [h3, Bool.false_eq_true, ite_false]
            by_cases h4 : (dec.op == "call") = true
            · simp only [h4, ite_true]; exact callRule_avoids hrd hwr _
            · simp only [h4, Bool.false_eq_true, ite_false]
              by_cases h5 : (dec.op == "return") = true
              · simp only [h5, ite_true]; exact returnRule_avoids vid pi dec.rdOps dec.wrOps _ T
              · simp only [h5, Bool.false_eq_true, ite_false]
                by_cases h6 : R.caps dec.op = true
                · simp only [h6, ite_true]
                  exact nativeRule_avoids (hR.nativeH1 dec.op h6) hrd hwr _
                · simp only [h6, Bool.false_eq_true, ite_false]
                  have nonDeleg : ∀ ιh, ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
                      (do
                        let vHdr ← Prog.rd (V := Val D) (.libHeader dec.op)
                        match vHdr with
                        | some (.routeHdr _) =>
                            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
                        | some .rwfuncHdr =>
                            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
                        | _ => pure (.error "unmatched_opcode")) ιh T := by
                    intro ιh
                    apply ExecAvoids.rd_bind (by simp [IsRho])
                    cases T (.libHeader dec.op) with
                    | none => exact ExecAvoids.ret _ _ _
                    | some vHdr =>
                      cases vHdr <;> try exact ExecAvoids.ret _ _ _
                      · exact userCallRule_avoids dec.op hrd hwr ιh
                      · exact handoffRule_avoids (hR.handoffH2 dec.op) dec.op vid pi dec.rdOps dec.wrOps ιh T
                  apply ExecAvoids.rd_bind (by simp [IsRho])
                  cases T (.delegEntry dec.op) with
                  | none => exact nonDeleg _
                  | some vDel =>
                    cases vDel <;> try exact nonDeleg _
                    exact delegRule_avoids (hR.delegH2 dec.op) dec.op vid pi dec.rdOps dec.wrOps _ T

theorem finalizeStep_avoids_rho_lambda (vid : Vid) (res : Except ErrMsg (PC × List E))
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (finalizeStep (D := D) (I := I) vid res) ι T := by
  cases res with
  | ok _ => exact ExecAvoids.ret _ _ _
  | error err =>
    unfold finalizeStep
    have hMsg := put_avoids_rl (vid := vid) (k := .msgKey vid)
      (by rintro (h | h) <;> cases h) (.errMsg err : Val D)
    apply ExecAvoids.wr_bind hMsg
    exact ExecAvoids.wr_bind (not_rl_status vid .error) (ExecAvoids.ret _ _ _)

theorem pauseAfterGate_avoids_rho_lambda (vid : Vid)
    (ι : Nat → I) (T : Store Key (Val D)) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (pauseAfterGate (D := D) (I := I) (E := E) vid) ι T := by
  unfold pauseAfterGate
  exact ExecAvoids.wr_bind (not_rl_status vid .paused) (ExecAvoids.ret _ _ _)

/-- Continuation of `gatedStep R vid` immediately after the gate read `rd(rho)`. -/
def gatedContinuation (R : Runtime D I E) (vid : Vid) :
    Option (Val D) → Prog Key (Val D) I E (StepOutcome E)
  | some _ => pauseAfterGate vid
  | none => do
      let res ← stepCore R vid
      finalizeStep vid res

theorem gatedStep_eq_read_continuation (R : Runtime D I E) (vid : Vid) :
    gatedStep R vid = .read (.stopKey vid) (gatedContinuation R vid) := rfl

theorem gatedContinuation_avoids_rho_lambda {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)} (hP : StepDiscipline vid T)
    (vRho : Option (Val D)) (ι : Nat → I) :
    ExecAvoids (IsRho vid) (IsRhoOrLambda vid)
      (gatedContinuation R vid vRho) ι T := by
  cases vRho with
  | some _ => exact pauseAfterGate_avoids_rho_lambda vid ι T
  | none =>
    unfold gatedContinuation
    apply ExecAvoids.bind_op (stepCore_avoids_rho_lambda hR hP ι)
    exact finalizeStep_avoids_rho_lambda vid _ _ _

/-- **Footprint Lemma (Model, T2)**:
every run of `gatedStep R vid` begins with the gate read `.read (.stopKey vid) _`;
no subsequent action in the transition reads `rho` (`..stop`), and no action
anywhere in the transition writes `rho` (`..stop`) or `lambda` (`..driver`). -/
theorem gatedStep_footprint {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)} (hP : StepDiscipline vid T) (ι : Nat → I) :
    ∃ rest,
      (exec (gatedStep R vid) ι T).2 =
        .read (.stopKey vid) (T (.stopKey vid)) :: rest ∧
      (∀ k v, Act.read k v ∈ rest → k ≠ .stopKey vid) ∧
      (∀ w, Act.write w ∈ (.read (.stopKey vid) (T (.stopKey vid)) :: rest) →
        ∀ k, w.affects k = true → k ≠ .stopKey vid ∧ k ≠ .driverKey vid) := by
  have hav := gatedContinuation_avoids_rho_lambda hR hP (T (.stopKey vid)) ι
  refine ⟨(exec (gatedContinuation R vid (T (.stopKey vid))) ι T).2, ?_, ?_, ?_⟩
  · rw [gatedStep_eq_read_continuation]; rfl
  · intro k v hmem
    exact hav.1 k v hmem
  · intro w hmem k hk
    simp only [List.mem_cons] at hmem
    rcases hmem with h | hmem
    · cases h
    · have hnot := hav.2 w hmem k hk
      exact ⟨fun h1 => hnot (Or.inl h1), fun h2 => hnot (Or.inr h2)⟩

end FootprintProofs

/-! ## Lipton Right-Mover Reduction on Live Action-Level Runs (Theorem 1 v2) -/

section LiptonReduction

variable {D I E : Type}

/-- Atomic stop write `stop(vid) = put(rho, "1")` (`SPEC-v2.md`). -/
def stopWrite (vid : Vid) : Write Key (Val D) :=
  .put (.stopKey vid) .stopMark

/-- Apply `n` consecutive `stop(vid)` writes to store `T` (none deleted). -/
def applyStops (vid : Vid) : Nat → Store Key (Val D) → Store Key (Val D)
  | 0, T => T
  | n + 1, T => (stopWrite vid).apply (applyStops vid n T)

@[simp] theorem applyStops_zero (vid : Vid) (T : Store Key (Val D)) :
    applyStops vid 0 T = T := rfl

theorem applyStops_succ_right (vid : Vid) (n : Nat) (T : Store Key (Val D)) :
    applyStops vid (n + 1) T = applyStops vid n ((stopWrite vid).apply T) := by
  induction n with
  | zero => rfl
  | succ n ih =>
    simp only [applyStops]
    exact congrArg (stopWrite vid).apply ih

theorem applyStops_other_key (vid : Vid) (n : Nat) (T : Store Key (Val D))
    {k : Key} (hk : k ≠ .stopKey vid) :
    applyStops vid n T k = T k := by
  induction n with
  | zero => rfl
  | succ n ih =>
    simp only [applyStops]
    rw [Write.apply_of_not_affects _ (by simp [stopWrite, Write.affects, hk]), ih]

theorem applyStops_pos_rho (vid : Vid) {n : Nat} (hn : 0 < n) (T : Store Key (Val D)) :
    applyStops vid n T (.stopKey vid) = some .stopMark := by
  cases n with
  | zero => omega
  | succ n =>
    simp only [applyStops]
    rw [Write.apply_of_affects _ (by simp [stopWrite, Write.affects])]
    rfl

/-- **Key Lipton Commutation Equation**: any store write `w` that does not
affect `rho` right-commutes past any number `n` of `stop(vid)` writes. -/
theorem write_commutes_applyStops {vid : Vid} {w : Write Key (Val D)}
    (hw : w.affects (.stopKey vid) = false) (n : Nat) (T : Store Key (Val D)) :
    w.apply (applyStops vid n T) = applyStops vid n (w.apply T) := by
  funext k
  by_cases hk : k = .stopKey vid
  · subst hk
    rw [Write.apply_of_not_affects _ hw]
    cases n with
    | zero => simp [Write.apply_of_not_affects _ hw]
    | succ n =>
      rw [applyStops_pos_rho vid (Nat.succ_pos n),
          applyStops_pos_rho vid (Nat.succ_pos n)]
  · rw [applyStops_other_key vid n _ hk]
    cases haff : w.affects k with
    | true => rw [Write.apply_of_affects _ haff, Write.apply_of_affects _ haff]
    | false =>
      rw [Write.apply_of_not_affects _ haff, Write.apply_of_not_affects _ haff,
          applyStops_other_key vid n _ hk]

/-- Micro-action inside an action-level execution: either a driver action or an
interleaved environment `stop = put(rho, 1)`. -/
inductive MicroAct (D I E : Type) where
  | drv (a : Act Key (Val D) I E)
  | envStop

namespace MicroAct

def driverActs : List (MicroAct D I E) → List (Act Key (Val D) I E)
  | [] => []
  | .drv a :: mas => a :: driverActs mas
  | .envStop :: mas => driverActs mas

def stopCount : List (MicroAct D I E) → Nat
  | [] => 0
  | .drv _ :: mas => stopCount mas
  | .envStop :: mas => 1 + stopCount mas

end MicroAct

/-- Live operational semantics of executing step sub-program `P` while the
environment injects `stop(vid)` actions at arbitrary points, immediately
updating the live store `T` seen by all subsequent reads of `P`. -/
inductive ValidActionExec {α : Type} (vid : Vid) :
    Prog Key (Val D) I E α → (Nat → I) → Store Key (Val D) →
    List (MicroAct D I E) → α → Store Key (Val D) → Prop where
  | ret (a : α) (ι : Nat → I) (T : Store Key (Val D)) :
      ValidActionExec vid (.ret a) ι T [] a T
  | envStop {P : Prog Key (Val D) I E α} {ι : Nat → I} {T T' : Store Key (Val D)}
      {mas : List (MicroAct D I E)} {res : α} :
      ValidActionExec vid P ι ((stopWrite vid).apply T) mas res T' →
      ValidActionExec vid P ι T (.envStop :: mas) res T'
  | read {k : Key} {next : Option (Val D) → Prog Key (Val D) I E α}
      {ι : Nat → I} {T T' : Store Key (Val D)}
      {mas : List (MicroAct D I E)} {res : α} :
      ValidActionExec vid (next (T k)) ι T mas res T' →
      ValidActionExec vid (.read k next) ι T (.drv (.read k (T k)) :: mas) res T'
  | write {w : Write Key (Val D)} {next : Prog Key (Val D) I E α}
      {ι : Nat → I} {T T' : Store Key (Val D)}
      {mas : List (MicroAct D I E)} {res : α} :
      ValidActionExec vid next ι (w.apply T) mas res T' →
      ValidActionExec vid (.write w next) ι T (.drv (.write w) :: mas) res T'
  | input {next : I → Prog Key (Val D) I E α}
      {ι : Nat → I} {T T' : Store Key (Val D)}
      {mas : List (MicroAct D I E)} {res : α} :
      ValidActionExec vid (next (ι 0)) (fun n => ι (n + 1)) T mas res T' →
      ValidActionExec vid (.input next) ι T (.drv (.input (ι 0)) :: mas) res T'
  | emit {e : E} {next : Prog Key (Val D) I E α}
      {ι : Nat → I} {T T' : Store Key (Val D)}
      {mas : List (MicroAct D I E)} {res : α} :
      ValidActionExec vid next ι T mas res T' →
      ValidActionExec vid (.emit e next) ι T (.drv (.emit e) :: mas) res T'

/-- **Lipton Right-Mover Lemma**: for any `Prog` `P` that neither reads nor
writes `rho` from `T_base`, any interleaved `ValidActionExec` starting from
`applyStops vid n T_base` performs the exact same driver actions and result as
`exec P ι T_base`, and ends in `applyStops vid (n + mas.stopCount) (after P ι T_base)`. -/
theorem validActionExec_right_mover_aux {α : Type} {vid : Vid}
    {P : Prog Key (Val D) I E α} {ι : Nat → I} {Tcur T' : Store Key (Val D)}
    {mas : List (MicroAct D I E)} {res : α}
    (hexec : ValidActionExec vid P ι Tcur mas res T') :
    ∀ {n : Nat} {Tbase : Store Key (Val D)},
      Tcur = applyStops vid n Tbase →
      ExecAvoids (IsRho vid) (IsRho vid) P ι Tbase →
      res = (exec P ι Tbase).1 ∧
      MicroAct.driverActs mas = (exec P ι Tbase).2 ∧
      T' = applyStops vid (n + MicroAct.stopCount mas) (after P ι Tbase) := by
  induction hexec with
  | ret a ι T =>
    intro n Tbase hcur _
    subst hcur
    exact ⟨rfl, rfl, by simp [MicroAct.stopCount]⟩
  | envStop _ ih =>
    intro n Tbase hcur hav
    subst hcur
    have hstep : (stopWrite (D := D) vid).apply (applyStops vid n Tbase) =
        applyStops vid (n + 1) Tbase := rfl
    obtain ⟨hres, hacts, hT'⟩ := ih hstep hav
    refine ⟨hres, hacts, ?_⟩
    rw [hT']
    exact congrArg (fun m => applyStops vid m (after _ _ Tbase))
      (by simp [MicroAct.stopCount]; omega)
  | @read k next ι T T' mas res _ ih =>
    intro n Tbase hcur hav
    have hkNotRho : k ≠ .stopKey vid :=
      hav.1 k (Tbase k) (by simp)
    have hkEq : T k = Tbase k := by
      rw [hcur]
      exact applyStops_other_key vid n Tbase hkNotRho
    have havNext : ExecAvoids (IsRho vid) (IsRho vid) (next (Tbase k)) ι Tbase := by
      refine ⟨?_, ?_⟩
      · intro k' v' hm; exact hav.1 k' v' (by simp [hm])
      · intro w hm k' haff; exact hav.2 w (by simp [hm]) k' haff
    rw [hkEq] at ih
    obtain ⟨hres, hacts, hT'⟩ := ih hcur havNext
    exact ⟨hres, by simp [MicroAct.driverActs, hkEq, hacts], hT'⟩
  | @write w next ι T T' mas res _ ih =>
    intro n Tbase hcur hav
    have hwNotRho : w.affects (.stopKey vid) = false := by
      cases haff : w.affects (.stopKey vid) with
      | false => rfl
      | true => exact absurd rfl (hav.2 w (by simp) (.stopKey vid) haff)
    have hcomm : w.apply T = applyStops vid n (w.apply Tbase) := by
      rw [hcur]
      exact write_commutes_applyStops hwNotRho n Tbase
    have havNext : ExecAvoids (IsRho vid) (IsRho vid) next ι (w.apply Tbase) := by
      refine ⟨?_, ?_⟩
      · intro k' v' hm; exact hav.1 k' v' (by simp [hm])
      · intro w' hm k' haff; exact hav.2 w' (by simp [hm]) k' haff
    obtain ⟨hres, hacts, hT'⟩ := ih hcomm havNext
    exact ⟨hres, by simp [MicroAct.driverActs, hacts], hT'⟩
  | @input next ι T T' mas res _ ih =>
    intro n Tbase hcur hav
    have havNext : ExecAvoids (IsRho vid) (IsRho vid) (next (ι 0)) (fun m => ι (m + 1)) Tbase := by
      refine ⟨?_, ?_⟩
      · intro k' v' hm; exact hav.1 k' v' (by simp [hm])
      · intro w hm k' haff; exact hav.2 w (by simp [hm]) k' haff
    obtain ⟨hres, hacts, hT'⟩ := ih hcur havNext
    exact ⟨hres, by simp [MicroAct.driverActs, hacts], hT'⟩
  | @emit e next ι T T' mas res _ ih =>
    intro n Tbase hcur hav
    have havNext : ExecAvoids (IsRho vid) (IsRho vid) next ι Tbase := by
      refine ⟨?_, ?_⟩
      · intro k' v' hm; exact hav.1 k' v' (by simp [hm])
      · intro w hm k' haff; exact hav.2 w (by simp [hm]) k' haff
    obtain ⟨hres, hacts, hT'⟩ := ih hcur havNext
    exact ⟨hres, by simp [MicroAct.driverActs, hacts], hT'⟩

/-- Specialized Lipton Right-Mover Lemma at `n = 0`: executing `gatedContinuation`
with arbitrary mid-step `stop`s yields the exact uninterrupted outcome and
driver actions, and leaves the post-step store with all `mas.stopCount` stops
moved to the end of the step. -/
theorem gatedContinuation_right_mover {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T T' : Store Key (Val D)} (hP : StepDiscipline vid T)
    {ι : Nat → I} {mas : List (MicroAct D I E)} {out : StepOutcome E}
    (hexec : ValidActionExec vid (gatedContinuation R vid (T (.stopKey vid))) ι T mas out T') :
    out = (exec (gatedStep R vid) ι T).1 ∧
    (.read (.stopKey vid) (T (.stopKey vid)) :: MicroAct.driverActs mas) =
      (exec (gatedStep R vid) ι T).2 ∧
    T' = applyStops vid (MicroAct.stopCount mas) (after (gatedStep R vid) ι T) := by
  have hav0 := gatedContinuation_avoids_rho_lambda (E := E) hR hP (T (.stopKey vid)) ι
  have havRho : ExecAvoids (IsRho vid) (IsRho vid)
      (gatedContinuation R vid (T (.stopKey vid))) ι T :=
    ⟨hav0.1, fun w hw k hk hr => hav0.2 w hw k hk (Or.inl hr)⟩
  obtain ⟨hout, hacts, hT'⟩ := validActionExec_right_mover_aux hexec (n := 0) rfl havRho
  rw [Nat.zero_add] at hT'
  refine ⟨?_, ?_, ?_⟩
  · rw [gatedStep_eq_read_continuation]; exact hout
  · rw [gatedStep_eq_read_continuation]; simp [hacts]
  · rw [gatedStep_eq_read_continuation]; exact hT'

/-! ## Multi-Step Action-Level vs Coarse-Level Runs (Theorem 1 v2) -/

/-- Store transformation of `resume(vid)`: `del(rho); put(sigma, running)`. -/
def resumeStore (vid : Vid) (T : Store Key (Val D)) : Store Key (Val D) :=
  applyAll T [.del (.stopKey vid), .put (.statusKey vid) (.status .running)]

/-- Precondition for `resume(vid)` (line 79): no driver holds
`lambda`, and `T(sigma) ∈ {paused, error}`. -/
def ResumeEnabled (vid : Vid) (T : Store Key (Val D)) : Prop :=
  T (.driverKey vid) = none ∧
  ∃ s, T (.statusKey vid) = some (.status s) ∧ s.isResumable = true

/-- Coarse-level run where stops only occur at step boundaries (between transitions). -/
inductive CoarseRun (R : Runtime D I E) (vid : Vid) :
    Store Key (Val D) → List (StepLabel E) → List (Store Key (Val D)) →
    Nat → Store Key (Val D) → Prop where
  | nil (T : Store Key (Val D)) :
      CoarseRun R vid T [] [T] 0 T
  | stop {T TLast : Store Key (Val D)} {tr : List (StepLabel E)}
      {gates : List (Store Key (Val D))} {sc : Nat} :
      CoarseRun R vid ((stopWrite vid).apply T) tr gates sc TLast →
      CoarseRun R vid T tr gates (sc + 1) TLast
  | resume {T TLast : Store Key (Val D)} {tr : List (StepLabel E)}
      {gates : List (Store Key (Val D))} {sc : Nat} :
      ResumeEnabled vid T →
      CoarseRun R vid (resumeStore vid T) tr gates sc TLast →
      CoarseRun R vid T tr gates sc TLast
  | gated {T TLast : Store Key (Val D)} {ι : Nat → I}
      {tr : List (StepLabel E)} {gates : List (Store Key (Val D))} {sc : Nat} :
      StoreRunnable vid T →
      StepDiscipline vid T →
      CoarseRun R vid (after (gatedStep R vid) ι T) tr gates sc TLast →
      CoarseRun R vid T
        (match (exec (gatedStep R vid) ι T).1.label? with
         | some lbl => lbl :: tr
         | none => tr)
        (T :: gates) sc TLast

/-- Prepend `k` coarse boundary stops starting from `T` before `rest`. -/
theorem CoarseRun.prependStops {R : Runtime D I E} {vid : Vid}
    (k : Nat) {T TLast : Store Key (Val D)} {tr : List (StepLabel E)}
    {gates : List (Store Key (Val D))} {sc : Nat}
    (hrest : CoarseRun R vid (applyStops vid k T) tr gates sc TLast) :
    CoarseRun R vid T tr gates (k + sc) TLast := by
  induction k generalizing T with
  | zero => simpa using hrest
  | succ k ih =>
    rw [applyStops_succ_right] at hrest
    have h1 := CoarseRun.stop (ih hrest)
    simpa [Nat.succ_add] using h1

/-- Action-level run allowing `stop(vid)` both between transitions and inside
any gated step (after its initial `rd(rho)` gate read) or inside `resume`
(between `del(rho)` and `put(sigma, running)`). -/
inductive ActionRun (R : Runtime D I E) (vid : Vid) :
    Store Key (Val D) → List (StepLabel E) → List (Store Key (Val D)) →
    Nat → Store Key (Val D) → Prop where
  | nil (T : Store Key (Val D)) :
      ActionRun R vid T [] [T] 0 T
  | stopBetween {T TLast : Store Key (Val D)} {tr : List (StepLabel E)}
      {gates : List (Store Key (Val D))} {sc : Nat} :
      ActionRun R vid ((stopWrite vid).apply T) tr gates sc TLast →
      ActionRun R vid T tr gates (sc + 1) TLast
  | resume {T TLast : Store Key (Val D)} (midStops : Nat)
      {tr : List (StepLabel E)} {gates : List (Store Key (Val D))} {sc : Nat} :
      ResumeEnabled vid T →
      ActionRun R vid
        ((Write.put (.statusKey vid) (.status .running)).apply
          (applyStops vid midStops ((Write.del (.stopKey vid)).apply T)))
        tr gates sc TLast →
      ActionRun R vid T tr gates (midStops + sc) TLast
  | gatedInFlight {T T₁ TLast : Store Key (Val D)} {ι : Nat → I}
      {mas : List (MicroAct D I E)} {out : StepOutcome E}
      {tr : List (StepLabel E)} {gates : List (Store Key (Val D))} {sc : Nat} :
      StoreRunnable vid T →
      StepDiscipline vid T →
      ValidActionExec vid (gatedContinuation R vid (T (.stopKey vid))) ι T mas out T₁ →
      ActionRun R vid T₁ tr gates sc TLast →
      ActionRun R vid T
        (match out.label? with
         | some lbl => lbl :: tr
         | none => tr)
        (T :: gates) (MicroAct.stopCount mas + sc) TLast

/-- **Theorem 1 (v2) (`theorem1_v2_reduction`)**:
for any action-level run `ActionRun R vid T tr gates sc TLast`, moving every
stop written inside a gated transition (or inside `resume`) to the end of that
transition produces a valid step-boundary `CoarseRun` with the **identical
step-label trace `tr`**, the **identical store at every gate/step boundary
`gates`**, the **identical final store `TLast`**, and the **identical stop count
`sc`** (no stop is deleted). -/
theorem theorem1_v2_reduction {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T TLast : Store Key (Val D)} {tr : List (StepLabel E)}
    {gates : List (Store Key (Val D))} {sc : Nat}
    (hAct : ActionRun R vid T tr gates sc TLast) :
    CoarseRun R vid T tr gates sc TLast := by
  induction hAct with
  | nil T => exact CoarseRun.nil T
  | stopBetween _ ih => exact CoarseRun.stop ih
  | @resume T TLast midStops tr gates sc hEn _ ih =>
    have hComm :
        (Write.put (K := Key) (V := Val D) (.statusKey vid) (.status .running)).apply
          (applyStops vid midStops ((Write.del (.stopKey vid)).apply T)) =
        applyStops vid midStops (resumeStore vid T) := by
      have hw : (Write.put (K := Key) (V := Val D) (.statusKey vid) (.status .running)).affects
          (.stopKey vid) = false := rfl
      simpa [resumeStore] using
        write_commutes_applyStops hw midStops ((Write.del (.stopKey vid)).apply T)
    rw [hComm] at ih
    exact CoarseRun.resume hEn (CoarseRun.prependStops midStops ih)
  | @gatedInFlight T T₁ TLast ι mas out tr gates sc hRun hP hexec _ ih =>
    obtain ⟨hout, _, hT₁⟩ := gatedContinuation_right_mover hR hP hexec
    rw [hout]
    rw [hT₁] at ih
    exact CoarseRun.gated hRun hP (CoarseRun.prependStops (MicroAct.stopCount mas) ih)

/-! ## Corollaries: Next-Gate Pause Guarantee -/

/-- Resume-free suffix of an `ActionRun` (no `resume` transition occurs). -/
inductive ResumeFreeActionRun (R : Runtime D I E) (vid : Vid) :
    Store Key (Val D) → List (StepOutcome E) → Store Key (Val D) → Prop where
  | nil (T : Store Key (Val D)) :
      ResumeFreeActionRun R vid T [] T
  | stopBetween {T TLast : Store Key (Val D)} {outs : List (StepOutcome E)} :
      ResumeFreeActionRun R vid ((stopWrite vid).apply T) outs TLast →
      ResumeFreeActionRun R vid T outs TLast
  | gatedInFlight {T T₁ TLast : Store Key (Val D)} {ι : Nat → I}
      {mas : List (MicroAct D I E)} {out : StepOutcome E}
      {outs : List (StepOutcome E)} :
      StoreRunnable vid T →
      StepDiscipline vid T →
      ValidActionExec vid (gatedContinuation R vid (T (.stopKey vid))) ι T mas out T₁ →
      ResumeFreeActionRun R vid T₁ outs TLast →
      ResumeFreeActionRun R vid T (out :: outs) TLast

/-- Once `T(rho)` is present (`some v`), any gated transition immediately pauses
(`out = .paused`) and preserves `rho` being present. -/
theorem gatedInFlight_pauses_when_rho_some {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T T₁ : Store Key (Val D)} (hP : StepDiscipline vid T)
    {v : Val D} (hrho : T (.stopKey vid) = some v)
    {ι : Nat → I} {mas : List (MicroAct D I E)} {out : StepOutcome E}
    (hexec : ValidActionExec vid (gatedContinuation R vid (T (.stopKey vid))) ι T mas out T₁) :
    out = .paused ∧ ∃ v', T₁ (.stopKey vid) = some v' := by
  obtain ⟨hout, _, hT₁⟩ := gatedContinuation_right_mover hR hP hexec
  have houtEq : (exec (gatedStep R vid) ι T).1 = .paused := by
    simp [gatedStep, pauseAfterGate, hrho]
  have hafterRho : after (gatedStep R vid) ι T (.stopKey vid) = some v := by
    simp [gatedStep, pauseAfterGate, after, hrho, Write.apply, Write.affects]
  refine ⟨hout.trans houtEq, ?_⟩
  rw [hT₁]
  cases hc : MicroAct.stopCount mas with
  | zero => exact ⟨v, by simpa using hafterRho⟩
  | succ m =>
    exact ⟨.stopMark, applyStops_pos_rho vid (Nat.succ_pos m) _⟩

/-- **Corollary to Theorem 1 (v2) (`stop_takes_effect_at_next_gate`)**:
once a `stop(vid)` has occurred (so `T(rho)` is `some _`), in any subsequent
execution without an intervening `resume`:
1. If at least one gate lies ahead (`outs = out₁ :: rest`), the **very first
   gate** takes the `Pause` transition (`out₁ = .paused`);
2. **Every** gated transition in `outs` pauses (zero steps execute or fail
   before an explicit `resume`). -/
theorem stop_takes_effect_at_next_gate {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T TLast : Store Key (Val D)} {outs : List (StepOutcome E)}
    (hrho : ∃ v, T (.stopKey vid) = some v)
    (hRun : ResumeFreeActionRun R vid T outs TLast) :
    (∀ out ∈ outs, out = .paused) ∧ (∃ v', TLast (.stopKey vid) = some v') := by
  induction hRun with
  | nil _ => exact ⟨by simp, hrho⟩
  | stopBetween _ ih =>
    apply ih
    exact ⟨.stopMark, by simp [stopWrite, Write.apply, Write.affects, Write.newVal]⟩
  | gatedInFlight _ hP hexec _ ih =>
    obtain ⟨v, hv⟩ := hrho
    obtain ⟨hpause, hrho₁⟩ := gatedInFlight_pauses_when_rho_some hR hP hv hexec
    obtain ⟨hall, hlast⟩ := ih hrho₁
    refine ⟨?_, hlast⟩
    intro o ho
    rcases List.mem_cons.1 ho with rfl | ho'
    · exact hpause
    · exact hall o ho'

/-- **In-Flight Stop Corollary (`midstep_stop_pauses_next_gate`)**:
if at least one `stop(vid)` occurs *inside* an in-flight gated transition
(`0 < MicroAct.stopCount mas`), then after that transition finishes, if any gate
lies ahead without an intervening `resume`, the very first gate pauses (`out = .paused`)
and zero subsequent steps execute. -/
theorem midstep_stop_pauses_next_gate {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T T₁ TLast : Store Key (Val D)} {ι : Nat → I}
    {mas : List (MicroAct D I E)} {out₀ : StepOutcome E}
    {outs : List (StepOutcome E)}
    (hP : StepDiscipline vid T)
    (hStop : 0 < MicroAct.stopCount mas)
    (hexec : ValidActionExec vid (gatedContinuation R vid (T (.stopKey vid))) ι T mas out₀ T₁)
    (hAfter : ResumeFreeActionRun R vid T₁ outs TLast) :
    ∀ o ∈ outs, o = .paused := by
  obtain ⟨_, _, hT₁⟩ := gatedContinuation_right_mover hR hP hexec
  have hrho₁ : ∃ v, T₁ (.stopKey vid) = some v :=
    ⟨.stopMark, by rw [hT₁]; exact applyStops_pos_rho vid hStop _⟩
  exact (stop_takes_effect_at_next_gate hR hrho₁ hAfter).1

end LiptonReduction

end Pathlang
