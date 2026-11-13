import Pathlang.Invariants
import Pathlang.StepCrash

/-!
# Operand Locality (`lem:locality`, Appendix A.3)

Mechanizes **Lemma `lem:locality`** (Appendix A.3): every store action (`read`
or `write`) performed by a `gatedStep` of thread `vid` from store `T` acts on a
path in one of the three locality regions:
1. **(i) Operand paths and pointer chains**: a path named by a resolved read or
   write operand of the current instruction, or read along its pointer
   resolution chain (or touched by a delegated/routed extension bounded by
   hypothesis `(H2)`);
2. **(ii) Instruction neighbourhood**: thread control keys (`rho`, `kappa`,
   `sigma`, `mu`), the current coordinate's `/lib` instruction/operand cells,
   the current frame `[d]` (with its `..lib`, `..callpc`, `..returnpc`), and
   the child frame `[d + 1]`; or
3. **(iii) Dispatch and signature metadata**: a callee's signature row
   `/lib/{callee}/[0,0]`, the operation's route header `/lib/{op}`, delegation
   entry `/sys/rwir-backend/{op}`, or the thread's queue entry
   `/lib/{op}/vids/{vid}`.

We prove both the strict non-extension form (`gatedStep_locality_no_ext`, under
`NotHandoffOrDeleg`) and the full runtime form (`gatedStep_locality`, adding the
`(H2)` bound for `Deleg` and `Handoff`).
-/

namespace Pathlang

section Locality

variable {D I E : Type}

/-- Region (i) of `lem:locality`: paths resolved by or traversed along the
pointer chain of an operand of `dec` at `(vid, pi)` in `T`. -/
def InOperandChainOrTarget (vid : Vid) (pi : PC) (dec : DecodedInstr D)
    (T : Store Key (Val D)) (k : Key) : Prop :=
  (∃ x ∈ dec.rdOps, k ∈ resolveReadReads vid pi x T ∨ resolveReadPure vid pi x T = some k) ∨
  (∃ y ∈ dec.wrOps, k ∈ resolveWriteReads vid pi y T ∨ resolveWritePure vid pi y T = some k)

/-- Region (ii) of `lem:locality`: instruction neighbourhood of `vid` in `T`
(gate/status/message/counter keys, current frame `[d]`, child frame `[d + 1]`,
and current coordinate `/lib/{fn}/[row, ...]`). -/
def InInstrNeighbourhood (vid : Vid) (T : Store Key (Val D)) (k : Key) : Prop :=
  k = .stopKey vid ∨ k = .pcKey vid ∨ k = .statusKey vid ∨ k = .msgKey vid ∨
  ∃ pi, T (.pcKey vid) = some (.pc pi) ∧
    (inFrame vid pi.depth k = true ∨
     inFrame vid (pi.depth + 1) k = true ∨
     ∃ fn, T (.frameLib vid pi.depth) = some (.sym fn) ∧
       (k = .libInstr fn pi.row ∨ ∃ dir idx, k = .libOperand fn pi.row dir idx))

/-- Region (iii) of `lem:locality`: callee signature row `/lib/{callee}/[0,0]`,
route header `/lib/{op}`, delegation entry `/sys/rwir-backend/{op}`, or queue
entry `/lib/{op}/vids/{vid}`. -/
def InDispatchMetadata (vid : Vid) (pi : PC) (dec : DecodedInstr D)
    (T : Store Key (Val D)) (k : Key) : Prop :=
  k = .delegEntry dec.op ∨
  k = .libHeader dec.op ∨
  k = .queueEntry dec.op vid ∨
  k = .libSig dec.op ∨
  (dec.op = "call" ∧ ∃ fnOp argRdOps pFn callee,
    dec.rdOps = fnOp :: argRdOps ∧
    resolveReadPure vid pi fnOp T = some pFn ∧
    T pFn = some (.sym callee) ∧
    k = .libSig callee)

/-- Combined core locality footprint (regions (i), (ii), and (iii) of
`lem:locality`). -/
def StepCoreLocal (vid : Vid) (T : Store Key (Val D)) (k : Key) : Prop :=
  InInstrNeighbourhood vid T k ∨
  ∃ pi dec, T (.pcKey vid) = some (.pc pi) ∧ decodeInstrPure vid pi T = .ok dec ∧
    (InOperandChainOrTarget vid pi dec T k ∨ InDispatchMetadata vid pi dec T k)

/-- Extension-bounded locality predicate for `Deleg` and `Handoff` under `(H2)`. -/
def InExtensionH2Footprint (R : Runtime D I E) (vid : Vid)
    (T : Store Key (Val D)) (k : Key) : Prop :=
  ∃ pi dec, T (.pcKey vid) = some (.pc pi) ∧ decodeInstrPure vid pi T = .ok dec ∧
    isCoreOp dec.op = false ∧ R.caps dec.op = false ∧
    ForbiddenKey vid pi.depth k = false

private abbrev NotLocal (P : Key → Prop) : Key → Prop := fun k => ¬ P k

private theorem avoid_of_mem {P : Key → Prop} {k : Key} (hk : P k) :
    ¬ NotLocal P k :=
  fun h => h hk

private theorem execAvoids_mono {K V : Type} [DecidableEq K] {α : Type}
    {P₁ P₂ : K → Prop} (hImp : ∀ k, P₁ k → P₂ k)
    {prog : Prog K V I E α} {ι : Nat → I} {S : Store K V}
    (h : ExecAvoids (fun k => ¬ P₁ k) (fun k => ¬ P₁ k) prog ι S) :
    ExecAvoids (fun k => ¬ P₂ k) (fun k => ¬ P₂ k) prog ι S :=
  ⟨fun k v hm h2 => h.1 k v hm (fun h1 => h2 (hImp k h1)),
   fun w hm k haff h2 => h.2 w hm k haff (fun h1 => h2 (hImp k h1))⟩

private theorem resolveRead_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    {x : Key × Val D} (hx : x ∈ dec.rdOps) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (resolveRead (I := I) (E := E) vid pi x) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hm
    rw [exec_resolveRead] at hm
    simp only [List.mem_map] at hm
    obtain ⟨r, hr, hAct⟩ := hm
    cases hAct
    exact avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inl ⟨x, hx, Or.inl hr⟩)⟩)
  · intro w hm
    rw [exec_resolveRead] at hm
    simp at hm

private theorem resolveWrite_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    {y : Key × Val D} (hy : y ∈ dec.wrOps) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (resolveWrite (I := I) (E := E) vid pi y) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hm
    rw [exec_resolveWrite] at hm
    simp only [List.mem_map] at hm
    obtain ⟨r, hr, hAct⟩ := hm
    cases hAct
    exact avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inr ⟨y, hy, Or.inl hr⟩)⟩)
  · intro w hm
    rw [exec_resolveWrite] at hm
    simp at hm

private theorem resolveAllRead_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) :
    ∀ (xs : List (Key × Val D)) (_hxs : ∀ x ∈ xs, x ∈ dec.rdOps) (ι : Nat → I),
      ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
        (resolveAllRead (I := I) (E := E) vid pi xs) ι T ∧
      after (resolveAllRead (I := I) (E := E) vid pi xs) ι T = T ∧
      (∀ ps, (exec (resolveAllRead (I := I) (E := E) vid pi xs) ι T).1 = some ps →
        ∀ p ∈ ps, StepCoreLocal vid T p)
  | [], _, _ =>
      ⟨ExecAvoids.ret _ _ _, rfl, fun ps hps p hp => by cases hps; simp at hp⟩
  | x :: xs, hxs, ι => by
      have hx := hxs x List.mem_cons_self
      have hrest := fun z hz => hxs z (List.mem_cons_of_mem _ hz)
      unfold resolveAllRead
      have hav1 := resolveRead_local (I := I) (E := E) hpc hdec hx ι
      have haf1 := after_resolveRead (I := I) (E := E) vid pi x ι T
      cases h1 : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
      | none =>
        refine ⟨ExecAvoids.bind_op hav1 (by rw [h1]; exact ExecAvoids.ret _ _ _), ?_, ?_⟩
        · rw [after_bind_op, h1, haf1]; rfl
        · intro ps hps; rw [exec_bind_fst, h1] at hps; simp at hps
      | some p =>
        have hpLoc : StepCoreLocal vid T p := by
          have hpPure : resolveReadPure vid pi x T = some p := by
            have h1' := h1; rw [exec_resolveRead] at h1'; exact h1'
          exact Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inl ⟨x, hx, Or.inr hpPure⟩)⟩
        let ι' := shiftInputs (resolveRead (I := I) (E := E) vid pi x) ι T
        obtain ⟨hav2, haf2, hps2⟩ := resolveAllRead_local hpc hdec xs hrest ι'
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
          dsimp only [] at hps
          rw [exec_bind_fst] at hps
          cases h2 : (exec (resolveAllRead (I := I) (E := E) vid pi xs) ι' T).1 with
          | none => dsimp only [ι'] at h2; simp [h2] at hps
          | some ps' =>
            have h2' := h2; dsimp only [ι'] at h2'; simp only [h2'] at hps
            cases hps
            rcases List.mem_cons.1 hq with rfl | hq'
            · exact hpLoc
            · exact hps2 ps' h2 q hq'

private theorem resolveAllWrite_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) :
    ∀ (ys : List (Key × Val D)) (_hys : ∀ y ∈ ys, y ∈ dec.wrOps) (ι : Nat → I),
      ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
        (resolveAllWrite (I := I) (E := E) vid pi ys) ι T ∧
      after (resolveAllWrite (I := I) (E := E) vid pi ys) ι T = T ∧
      (∀ qs, (exec (resolveAllWrite (I := I) (E := E) vid pi ys) ι T).1 = some qs →
        ∀ q ∈ qs, StepCoreLocal vid T q)
  | [], _, _ =>
      ⟨ExecAvoids.ret _ _ _, rfl, fun qs hqs q hq => by cases hqs; simp at hq⟩
  | y :: ys, hys, ι => by
      have hy := hys y List.mem_cons_self
      have hrest := fun z hz => hys z (List.mem_cons_of_mem _ hz)
      unfold resolveAllWrite
      have hav1 := resolveWrite_local (I := I) (E := E) hpc hdec hy ι
      have haf1 := after_resolveWrite (I := I) (E := E) vid pi y ι T
      cases h1 : (exec (resolveWrite (I := I) (E := E) vid pi y) ι T).1 with
      | none =>
        refine ⟨ExecAvoids.bind_op hav1 (by rw [h1]; exact ExecAvoids.ret _ _ _), ?_, ?_⟩
        · rw [after_bind_op, h1, haf1]; rfl
        · intro qs hqs; rw [exec_bind_fst, h1] at hqs; simp at hqs
      | some q =>
        have hqLoc : StepCoreLocal vid T q := by
          have hqPure : resolveWritePure vid pi y T = some q := by
            have h1' := h1; rw [exec_resolveWrite] at h1'; exact h1'
          exact Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inr ⟨y, hy, Or.inr hqPure⟩)⟩
        let ι' := shiftInputs (resolveWrite (I := I) (E := E) vid pi y) ι T
        obtain ⟨hav2, haf2, hqs2⟩ := resolveAllWrite_local hpc hdec ys hrest ι'
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
          dsimp only [] at hqs
          rw [exec_bind_fst] at hqs
          cases h2 : (exec (resolveAllWrite (I := I) (E := E) vid pi ys) ι' T).1 with
          | none => dsimp only [ι'] at h2; simp [h2] at hqs
          | some qs' =>
            have h2' := h2; dsimp only [ι'] at h2'; simp only [h2'] at hqs
            cases hqs
            rcases List.mem_cons.1 hq' with rfl | hq''
            · exact hqLoc
            · exact hqs2 qs' h2 q' hq''

private theorem resolveRowTarget_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    {x : Key × Val D} (hx : x ∈ dec.rdOps) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (resolveRowTarget (I := I) (E := E) vid pi x) ι T ∧
    after (resolveRowTarget (I := I) (E := E) vid pi x) ι T = T := by
  unfold resolveRowTarget
  have hav1 := resolveRead_local (I := I) (E := E) hpc hdec hx ι
  have haf1 := after_resolveRead (I := I) (E := E) vid pi x ι T
  cases h1 : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none =>
    refine ⟨ExecAvoids.bind_op hav1 (by rw [h1]; exact ExecAvoids.ret _ _ _), ?_⟩
    rw [after_bind_op, h1, haf1]; rfl
  | some p =>
    have hpPure : resolveReadPure vid pi x T = some p := by
      have h1' := h1; rw [exec_resolveRead] at h1'; exact h1'
    refine ⟨?_, ?_⟩
    · apply ExecAvoids.bind_op hav1
      rw [h1, haf1]
      apply ExecAvoids.rd_bind (avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inl ⟨x, hx, Or.inr hpPure⟩)⟩))
      cases T p with
      | none => exact ExecAvoids.ret _ _ _
      | some v =>
        cases v with
        | int z =>
          cases z with
          | ofNat m => cases m <;> exact ExecAvoids.ret _ _ _
          | negSucc _ => exact ExecAvoids.ret _ _ _
        | _ => exact ExecAvoids.ret _ _ _
    · rw [after_bind_op, h1, haf1]; dsimp only []
      rw [Prog.bind_eq, Prog.rd_bind, after_read]
      cases T p with
      | none => rfl
      | some v =>
        cases v with
        | int z =>
          cases z with
          | ofNat m => cases m <;> rfl
          | negSucc _ => rfl
        | _ => rfl

private theorem resolveTruth_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    {x : Key × Val D} (hx : x ∈ dec.rdOps) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (resolveTruth (I := I) (E := E) vid pi x) ι T ∧
    after (resolveTruth (I := I) (E := E) vid pi x) ι T = T := by
  unfold resolveTruth
  have hav1 := resolveRead_local (I := I) (E := E) hpc hdec hx ι
  have haf1 := after_resolveRead (I := I) (E := E) vid pi x ι T
  cases h1 : (exec (resolveRead (I := I) (E := E) vid pi x) ι T).1 with
  | none =>
    refine ⟨ExecAvoids.bind_op hav1 (by rw [h1]; exact ExecAvoids.ret _ _ _), ?_⟩
    rw [after_bind_op, h1, haf1]; rfl
  | some p =>
    have hpPure : resolveReadPure vid pi x T = some p := by
      have h1' := h1; rw [exec_resolveRead] at h1'; exact h1'
    refine ⟨?_, ?_⟩
    · apply ExecAvoids.bind_op hav1
      rw [h1, haf1]
      apply ExecAvoids.rd_bind (avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inl ⟨x, hx, Or.inr hpPure⟩)⟩))
      cases T p with
      | none => exact ExecAvoids.ret _ _ _
      | some v => cases v <;> exact ExecAvoids.ret _ _ _
    · rw [after_bind_op, h1, haf1]; dsimp only []
      rw [Prog.bind_eq, Prog.rd_bind, after_read]
      cases T p with
      | none => rfl
      | some v => cases v <;> rfl

private theorem readOperandsFrom_local {vid : Vid} {pi : PC} {fn : SymName}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hLib : T (.frameLib vid pi.depth) = some (.sym fn))
    (dir : ParamDir) :
    ∀ (idx count : Nat) (ι : Nat → I),
      ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
        (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row dir idx count) ι T
  | _, 0, _ => ExecAvoids.ret _ _ _
  | idx, count + 1, ι => by
      unfold readOperandsFrom
      apply ExecAvoids.rd_bind
      · exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inr (Or.inr
          ⟨pi, hpc, Or.inr (Or.inr ⟨fn, hLib, Or.inr ⟨dir, idx, rfl⟩⟩)⟩)))))
      · cases T (.libOperand fn pi.row dir idx) with
        | none => exact ExecAvoids.ret _ _ _
        | some v =>
          apply ExecAvoids.bind_op (readOperandsFrom_local hpc hLib dir (idx + 1) count _)
          rw [after_readOperandsFrom]
          cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row dir (idx + 1) count) _ T).1 <;>
            exact ExecAvoids.ret _ _ _

private theorem decodeInstr_local {vid : Vid} {pi : PC} {T : Store Key (Val D)}
    (hpc : T (.pcKey vid) = some (.pc pi)) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (decodeInstr (D := D) (I := I) (E := E) vid pi) ι T := by
  unfold decodeInstr
  cases pi.valid with
  | false => exact ExecAvoids.ret _ _ _
  | true =>
    simp only [Bool.not_true, Bool.false_eq_true, ite_false]
    apply ExecAvoids.rd_bind
    · exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inr (Or.inr
        ⟨pi, hpc, Or.inl (by simp [inFrame])⟩)))))
    · cases hLib : T (.frameLib vid pi.depth) with
      | none => exact ExecAvoids.ret _ _ _
      | some vLib =>
        cases vLib <;> try exact ExecAvoids.ret _ _ _
        rename_i fn
        apply ExecAvoids.rd_bind
        · exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inr (Or.inr
            ⟨pi, hpc, Or.inr (Or.inr ⟨fn, hLib, Or.inl rfl⟩)⟩)))))
        · cases T (.libInstr fn pi.row) with
          | none => exact ExecAvoids.ret _ _ _
          | some vIns =>
            cases vIns <;> try exact ExecAvoids.ret _ _ _
            rename_i op nr nw
            apply ExecAvoids.bind_op (readOperandsFrom_local hpc hLib .rd 1 nr _)
            rw [after_readOperandsFrom]
            cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .rd 1 nr) _ T).1 with
            | none => exact ExecAvoids.ret _ _ _
            | some rdOps =>
              apply ExecAvoids.bind_op (readOperandsFrom_local hpc hLib .wr 1 nw _)
              rw [after_readOperandsFrom]
              cases (exec (readOperandsFrom (D := D) (I := I) (E := E) fn pi.row .wr 1 nw) _ T).1 <;>
                exact ExecAvoids.ret _ _ _

private theorem bindReadArgs_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) :
    ∀ (idx : Nat) (xs : List (Key × Val D)) (ι : Nat → I) (S : Store Key (Val D)),
      (∀ x ∈ xs, x ∈ dec.rdOps ∧ ReadOpSafe vid pi T x) →
      (∀ k, inFrame vid (pi.depth + 1) k = false → S k = T k) →
      ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
        (bindReadArgs (I := I) (E := E) vid pi idx xs) ι S ∧
      (∀ k, inFrame vid (pi.depth + 1) k = false →
        after (bindReadArgs (I := I) (E := E) vid pi idx xs) ι S k = T k)
  | _, [], _, S, _, hEq => ⟨ExecAvoids.ret _ _ _, hEq⟩
  | idx, x :: xs, ι, S, hxs, hEq => by
      obtain ⟨hxMem, hxSafe⟩ := hxs x List.mem_cons_self
      have hrest := fun z hz => hxs z (List.mem_cons_of_mem _ hz)
      have hCongr : exec (resolveRead (I := I) (E := E) vid pi x) ι S =
          exec (resolveRead (I := I) (E := E) vid pi x) ι T := by
        apply exec_congr _ _ T S (fun k => inFrame vid (pi.depth + 1) k = true)
        · intro k hk; exact hEq k (by simpa using hk)
        · intro k hexp
          obtain ⟨v, hm⟩ := Act.reads_of_exposed hexp
          rw [exec_resolveRead] at hm; simp only [List.mem_map] at hm
          obtain ⟨r, hr, hAct⟩ := hm; cases hAct
          simp [not_inChild_of_not_forbidden (hxSafe.1 k hr)]
      unfold bindReadArgs
      have havRd : ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
          (resolveRead (I := I) (E := E) vid pi x) ι S := by
        refine ⟨?_, ?_⟩
        · intro k v hm; rw [hCongr, exec_resolveRead] at hm
          simp only [List.mem_map] at hm; obtain ⟨r, hr, hAct⟩ := hm; cases hAct
          exact avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inl ⟨x, hxMem, Or.inl hr⟩)⟩)
        · intro w hm; rw [hCongr, exec_resolveRead] at hm; simp at hm
      cases hp : (exec (resolveRead (I := I) (E := E) vid pi x) ι S).1 with
      | none =>
        refine ⟨ExecAvoids.bind_op havRd (by rw [hp]; exact ExecAvoids.ret _ _ _), ?_⟩
        intro k hk
        rw [after_bind_op, hp, after_resolveRead]; exact hEq k hk
      | some p =>
        let S' := (Write.put (.frameParam vid (pi.depth + 1) .rd idx) (.ptr p)).apply S
        have hEq' : ∀ k, inFrame vid (pi.depth + 1) k = false → S' k = T k := by
          intro k hk
          have hne : k ≠ .frameParam vid (pi.depth + 1) .rd idx := by
            rintro rfl; simp [inFrame] at hk
          simp [S', Write.apply, Write.affects, hne, hEq k hk]
        obtain ⟨havRest, hAfterRest⟩ := bindReadArgs_local hpc hdec (idx + 1) xs _ S' hrest hEq'
        refine ⟨?_, ?_⟩
        · apply ExecAvoids.bind_op havRd
          rw [hp, after_resolveRead]
          apply ExecAvoids.wr_bind
          · intro k haff
            simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
            exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inr (Or.inr
              ⟨pi, hpc, Or.inr (Or.inl (by simp [inFrame]))⟩)))))
          · exact havRest
        · intro k hk
          rw [after_bind_op, hp, after_resolveRead]; dsimp only []
          rw [Prog.bind_eq, Prog.wr_bind, after_write]
          exact hAfterRest k hk

private theorem bindWriteArgs_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) :
    ∀ (idx : Nat) (ys : List (Key × Val D)) (ι : Nat → I) (S : Store Key (Val D)),
      (∀ y ∈ ys, y ∈ dec.wrOps ∧ WriteOpSafe vid pi T y) →
      (∀ k, inFrame vid (pi.depth + 1) k = false → S k = T k) →
      ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
        (bindWriteArgs (I := I) (E := E) vid pi idx ys) ι S
  | _, [], _, _, _, _ => ExecAvoids.ret _ _ _
  | idx, y :: ys, ι, S, hys, hEq => by
      obtain ⟨hyMem, hySafe⟩ := hys y List.mem_cons_self
      have hrest := fun z hz => hys z (List.mem_cons_of_mem _ hz)
      have hCongr : exec (resolveWrite (I := I) (E := E) vid pi y) ι S =
          exec (resolveWrite (I := I) (E := E) vid pi y) ι T := by
        apply exec_congr _ _ T S (fun k => inFrame vid (pi.depth + 1) k = true)
        · intro k hk; exact hEq k (by simpa using hk)
        · intro k hexp
          obtain ⟨v, hm⟩ := Act.reads_of_exposed hexp
          rw [exec_resolveWrite] at hm; simp only [List.mem_map] at hm
          obtain ⟨r, hr, hAct⟩ := hm; cases hAct
          simp [not_inChild_of_not_forbidden (hySafe.1 k hr)]
      unfold bindWriteArgs
      have havWr : ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
          (resolveWrite (I := I) (E := E) vid pi y) ι S := by
        refine ⟨?_, ?_⟩
        · intro k v hm; rw [hCongr, exec_resolveWrite] at hm
          simp only [List.mem_map] at hm; obtain ⟨r, hr, hAct⟩ := hm; cases hAct
          exact avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inr ⟨y, hyMem, Or.inl hr⟩)⟩)
        · intro w hm; rw [hCongr, exec_resolveWrite] at hm; simp at hm
      cases hq : (exec (resolveWrite (I := I) (E := E) vid pi y) ι S).1 with
      | none =>
        exact ExecAvoids.bind_op havWr (by rw [hq]; exact ExecAvoids.ret _ _ _)
      | some q =>
        let S' := (Write.put (.frameParam vid (pi.depth + 1) .wr idx) (.ptr q)).apply S
        have hEq' : ∀ k, inFrame vid (pi.depth + 1) k = false → S' k = T k := by
          intro k hk
          have hne : k ≠ .frameParam vid (pi.depth + 1) .wr idx := by
            rintro rfl; simp [inFrame] at hk
          simp [S', Write.apply, Write.affects, hne, hEq k hk]
        apply ExecAvoids.bind_op havWr
        rw [hq, after_resolveWrite]
        apply ExecAvoids.wr_bind
        · intro k haff
          simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
          exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inr (Or.inr
            ⟨pi, hpc, Or.inr (Or.inl (by simp [inFrame]))⟩)))))
        · exact bindWriteArgs_local hpc hdec (idx + 1) ys _ S' hrest hEq'

private theorem bindChild_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {callee : SymName} {rdArgs wrArgs : List (Key × Val D)}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hSigLoc : StepCoreLocal vid T (.libSig callee))
    (hrd : ∀ x ∈ rdArgs, x ∈ dec.rdOps ∧ ReadOpSafe vid pi T x)
    (hwr : ∀ y ∈ wrArgs, y ∈ dec.wrOps ∧ WriteOpSafe vid pi T y)
    (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (bindChild (I := I) (E := E) vid pi callee rdArgs wrArgs) ι T := by
  unfold bindChild
  apply ExecAvoids.rd_bind (avoid_of_mem hSigLoc)
  cases T (.libSig callee) with
  | none => exact ExecAvoids.ret _ _ _
  | some vSig =>
    cases vSig <;> try exact ExecAvoids.ret _ _ _
    rename_i sigNr sigNw
    cases har : (sigNr != rdArgs.length || sigNw != wrArgs.length) with
    | true => simp only [har, ite_true]; exact ExecAvoids.ret _ _ _
    | false =>
      simp only [har, Bool.false_eq_true, ite_false]
      have childLoc : ∀ k, inFrame vid (pi.depth + 1) k = true →
          ¬ NotLocal (StepCoreLocal vid T) k :=
        fun _ hk => avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inr (Or.inr
          ⟨pi, hpc, Or.inr (Or.inl hk)⟩)))))
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects] at haff; exact childLoc k haff
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact childLoc _ (by simp [inFrame])
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact childLoc _ (by simp [inFrame])
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact childLoc _ (by simp [inFrame])
      let S₀ := (Write.put (.frameCallPc vid (pi.depth + 1)) (.pc pi.entry)).apply
        ((Write.put (.frameRetPc vid (pi.depth + 1)) (.pc pi.next)).apply
          ((Write.put (.frameLib vid (pi.depth + 1)) (.sym callee)).apply
            ((Write.delWhere (inFrame vid (pi.depth + 1))).apply T)))
      have hS₀Eq : ∀ k, inFrame vid (pi.depth + 1) k = false → S₀ k = T k := by
        intro k hk
        have h1 : k ≠ .frameCallPc vid (pi.depth + 1) := by rintro rfl; simp [inFrame] at hk
        have h2 : k ≠ .frameRetPc vid (pi.depth + 1) := by rintro rfl; simp [inFrame] at hk
        have h3 : k ≠ .frameLib vid (pi.depth + 1) := by rintro rfl; simp [inFrame] at hk
        simp [S₀, Write.apply, Write.affects, hk, h1, h2, h3]
      obtain ⟨havRd, hAfterRd⟩ := bindReadArgs_local (I := I) (E := E) hpc hdec 1 rdArgs _ S₀ hrd hS₀Eq
      apply ExecAvoids.bind_op havRd
      cases (exec (bindReadArgs (I := I) (E := E) vid pi 1 rdArgs) _ S₀).1 with
      | error _ => exact ExecAvoids.ret _ _ _
      | ok u =>
        cases u
        exact bindWriteArgs_local hpc hdec 1 wrArgs _ _ hwr hAfterRd

private theorem readKeyList_local {P : Key → Prop} :
    ∀ (ks : List Key) (_hks : ∀ k ∈ ks, P k) (ι : Nat → I) (S : Store Key (Val D)),
      ExecAvoids (NotLocal P) (NotLocal P) (readKeyList (D := D) (I := I) (E := E) ks) ι S
  | [], _, _, _ => ExecAvoids.ret _ _ _
  | k :: ks, hks, ι, S => by
      unfold readKeyList
      apply ExecAvoids.rd_bind (avoid_of_mem (hks k List.mem_cons_self))
      apply ExecAvoids.bind_op (readKeyList_local ks (fun z hz => hks z (List.mem_cons_of_mem _ hz)) _ S)
      exact ExecAvoids.ret _ _ _

private theorem recvMany_local {P : Key → Prop} :
    ∀ (n : Nat) (ι : Nat → I) (S : Store Key (Val D)),
      ExecAvoids (NotLocal P) (NotLocal P) (recvMany (D := D) (I := I) (E := E) n) ι S
  | 0, _, _ => ExecAvoids.ret _ _ _
  | n + 1, ι, S => by
      unfold recvMany
      apply ExecAvoids.recv_bind
      apply ExecAvoids.bind_op (recvMany_local n _ S)
      exact ExecAvoids.ret _ _ _

private theorem emitAll_local {P : Key → Prop} :
    ∀ (evs : List E) (ι : Nat → I) (S : Store Key (Val D)),
      ExecAvoids (NotLocal P) (NotLocal P) (emitAll (D := D) (I := I) (E := E) evs) ι S
  | [], _, _ => ExecAvoids.ret _ _ _
  | e :: evs, _, _ => by
      unfold emitAll
      apply ExecAvoids.out_bind
      exact emitAll_local evs _ _

private theorem putAll_local {P : Key → Prop} :
    ∀ (kvs : List (Key × Val D)) (_hkvs : ∀ k v, (k, v) ∈ kvs → P k)
      (ι : Nat → I) (S : Store Key (Val D)),
      ExecAvoids (NotLocal P) (NotLocal P) (putAll (D := D) (I := I) (E := E) kvs) ι S
  | [], _, _, _ => ExecAvoids.ret _ _ _
  | kv :: kvs, hkvs, _, _ => by
      rcases kv with ⟨k, v⟩
      have hkv : P k := hkvs k v List.mem_cons_self
      unfold putAll
      apply ExecAvoids.wr_bind
      · intro k' haff
        simp only [Write.affects, decide_eq_true_eq] at haff
        cases haff
        exact avoid_of_mem hkv
      · exact putAll_local kvs (fun k' v' hm => hkvs k' v' (List.mem_cons_of_mem _ hm)) _ _

private theorem copyRule_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (copyRuleProg (I := I) (E := E) vid pi dec.rdOps dec.wrOps) ι T := by
  unfold copyRuleProg
  match hrdOps : dec.rdOps, hwrOps : dec.wrOps with
  | [rd1], [wr1] =>
    have hx : rd1 ∈ dec.rdOps := by simp [hrdOps]
    have hy : wr1 ∈ dec.wrOps := by simp [hwrOps]
    dsimp only []
    apply ExecAvoids.bind_op (resolveRead_local hpc hdec hx ι)
    rw [after_resolveRead]
    cases hp : (exec (resolveRead (I := I) (E := E) vid pi rd1) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some p =>
      have hpPure : resolveReadPure vid pi rd1 T = some p := by
        have hp' := hp; rw [exec_resolveRead] at hp'; exact hp'
      apply ExecAvoids.bind_op (resolveWrite_local hpc hdec hy _)
      rw [after_resolveWrite]
      cases hq : (exec (resolveWrite (I := I) (E := E) vid pi wr1) _ T).1 with
      | none => exact ExecAvoids.ret _ _ _
      | some q =>
        have hqPure : resolveWritePure vid pi wr1 T = some q := by
          have hq' := hq; rw [exec_resolveWrite] at hq'; exact hq'
        apply ExecAvoids.rd_bind
          (avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inl ⟨rd1, hx, Or.inr hpPure⟩)⟩))
        cases T p with
        | none => exact ExecAvoids.ret _ _ _
        | some val =>
          apply ExecAvoids.wr_bind
          · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
            exact avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inr ⟨wr1, hy, Or.inr hqPure⟩)⟩)
          apply ExecAvoids.wr_bind
          · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
            exact avoid_of_mem (Or.inl (Or.inr (Or.inl rfl)))
          exact ExecAvoids.ret _ _ _
  | [], _ => exact ExecAvoids.ret _ _ _
  | _ :: _ :: _, _ => exact ExecAvoids.ret _ _ _
  | [_], [] => exact ExecAvoids.ret _ _ _
  | [_], _ :: _ :: _ => exact ExecAvoids.ret _ _ _

private theorem gotoRule_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (gotoRuleProg (I := I) (E := E) vid pi dec.rdOps dec.wrOps) ι T := by
  unfold gotoRuleProg
  match hrdOps : dec.rdOps, hwrOps : dec.wrOps with
  | [rd1], [] =>
    have hx : rd1 ∈ dec.rdOps := by simp [hrdOps]
    obtain ⟨hav, haf⟩ := resolveRowTarget_local (I := I) (E := E) hpc hdec hx ι
    dsimp only []
    apply ExecAvoids.bind_op hav
    rw [haf]
    cases (exec (resolveRowTarget (I := I) (E := E) vid pi rd1) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some n =>
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem (Or.inl (Or.inr (Or.inl rfl)))
      exact ExecAvoids.ret _ _ _
  | [], _ => exact ExecAvoids.ret _ _ _
  | _ :: _ :: _, _ => exact ExecAvoids.ret _ _ _
  | [_], _ :: _ => exact ExecAvoids.ret _ _ _

private theorem brRule_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (brRuleProg (I := I) (E := E) vid pi dec.rdOps dec.wrOps) ι T := by
  unfold brRuleProg
  match hrdOps : dec.rdOps, hwrOps : dec.wrOps with
  | [rd1, rd2, rd3], [] =>
    have hx1 : rd1 ∈ dec.rdOps := by simp [hrdOps]
    obtain ⟨hav1, haf1⟩ := resolveTruth_local (I := I) (E := E) hpc hdec hx1 ι
    dsimp only []
    apply ExecAvoids.bind_op hav1
    rw [haf1]
    cases (exec (resolveTruth (I := I) (E := E) vid pi rd1) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some b =>
      have hxb : (if b then rd2 else rd3) ∈ dec.rdOps := by cases b <;> simp [hrdOps]
      obtain ⟨hav2, haf2⟩ := resolveRowTarget_local (I := I) (E := E) hpc hdec hxb _
      apply ExecAvoids.bind_op hav2
      rw [haf2]
      cases (exec (resolveRowTarget (I := I) (E := E) vid pi (if b then rd2 else rd3)) _ T).1 with
      | none => exact ExecAvoids.ret _ _ _
      | some n =>
        apply ExecAvoids.wr_bind
        · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
          exact avoid_of_mem (Or.inl (Or.inr (Or.inl rfl)))
        exact ExecAvoids.ret _ _ _
  | [], _ => exact ExecAvoids.ret _ _ _
  | [_], _ => exact ExecAvoids.ret _ _ _
  | [_, _], _ => exact ExecAvoids.ret _ _ _
  | _ :: _ :: _ :: _ :: _, _ => exact ExecAvoids.ret _ _ _
  | [_, _, _], _ :: _ => exact ExecAvoids.ret _ _ _

private theorem callRule_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) (hop : dec.op = "call")
    (hrdSafe : ∀ x ∈ dec.rdOps, ReadOpSafe vid pi T x)
    (hwrSafe : ∀ y ∈ dec.wrOps, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (callRuleProg (I := I) (E := E) vid pi dec.rdOps dec.wrOps) ι T := by
  unfold callRuleProg
  match hrdOps : dec.rdOps with
  | [] => exact ExecAvoids.ret _ _ _
  | fnOp :: argRdOps =>
    have hFnMem : fnOp ∈ dec.rdOps := by simp [hrdOps]
    dsimp only []
    apply ExecAvoids.bind_op (resolveRead_local hpc hdec hFnMem ι)
    rw [after_resolveRead]
    cases hpFn : (exec (resolveRead (I := I) (E := E) vid pi fnOp) ι T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some pFn =>
      have hpFnPure : resolveReadPure vid pi fnOp T = some pFn := by
        have hpFn' := hpFn; rw [exec_resolveRead] at hpFn'; exact hpFn'
      apply ExecAvoids.rd_bind
        (avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inl (Or.inl ⟨fnOp, hFnMem, Or.inr hpFnPure⟩)⟩))
      cases hvFn : T pFn with
      | none => exact ExecAvoids.ret _ _ _
      | some vFn =>
        cases vFn <;> try exact ExecAvoids.ret _ _ _
        rename_i callee
        have hSigLoc : StepCoreLocal vid T (.libSig callee) :=
          Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inr (Or.inr (Or.inr (Or.inr
            ⟨hop, fnOp, argRdOps, pFn, callee, hrdOps, hpFnPure, hvFn, rfl⟩))))⟩
        have hArgs : ∀ x ∈ argRdOps, x ∈ dec.rdOps ∧ ReadOpSafe vid pi T x :=
          fun x hx => ⟨by simp [hrdOps, hx], hrdSafe x (by simp [hrdOps, hx])⟩
        have hWrs : ∀ y ∈ dec.wrOps, y ∈ dec.wrOps ∧ WriteOpSafe vid pi T y :=
          fun y hy => ⟨hy, hwrSafe y hy⟩
        apply ExecAvoids.bind_op (bindChild_local hpc hdec hSigLoc hArgs hWrs _)
        cases (exec (bindChild (I := I) (E := E) vid pi callee argRdOps dec.wrOps) _ T).1 with
        | error _ => exact ExecAvoids.ret _ _ _
        | ok u =>
          cases u
          apply ExecAvoids.wr_bind
          · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
            exact avoid_of_mem (Or.inl (Or.inr (Or.inl rfl)))
          exact ExecAvoids.ret _ _ _

private theorem returnRule_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (returnRuleProg (I := I) (E := E) vid pi dec.rdOps dec.wrOps) ι T := by
  unfold returnRuleProg
  match dec.rdOps, dec.wrOps with
  | [], [] =>
    by_cases hd : 1 < pi.depth
    · simp only [hd, ite_true]
      apply ExecAvoids.rd_bind
      · exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inr (Or.inr
          ⟨pi, hpc, Or.inl (by simp [inFrame])⟩)))))
      · cases T (.frameRetPc vid pi.depth) with
        | none => exact ExecAvoids.ret _ _ _
        | some vRet =>
          cases vRet <;> try exact ExecAvoids.ret _ _ _
          rename_i r
          by_cases hrv : (r.valid && decide (r.depth + 1 = pi.depth)) = true
          · simp only [hrv, ite_true]
            apply ExecAvoids.wr_bind
            · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
              exact avoid_of_mem (Or.inl (Or.inr (Or.inl rfl)))
            apply ExecAvoids.wr_bind
            · intro k haff; simp only [Write.affects] at haff
              exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inr (Or.inr
                ⟨pi, hpc, Or.inl haff⟩)))))
            exact ExecAvoids.ret _ _ _
          · simp only [hrv, Bool.false_eq_true, ite_false]
            exact ExecAvoids.ret _ _ _
    · simp only [hd, ite_false]
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inl rfl))))
      exact ExecAvoids.ret _ _ _
  | _ :: _, _ => exact ExecAvoids.ret _ _ _
  | [], _ :: _ => exact ExecAvoids.ret _ _ _

private theorem nativeRule_local {h : NativeHandler D I E} (hH1 : HandlerSpec h)
    {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (nativeRuleProg h vid pi dec.rdOps dec.wrOps) ι T := by
  unfold nativeRuleProg
  obtain ⟨havRd, hafRd, hpsRd⟩ :=
    resolveAllRead_local (I := I) (E := E) hpc hdec dec.rdOps (fun _ hx => hx) ι
  apply ExecAvoids.bind_op havRd
  rw [hafRd]
  cases h1 : (exec (resolveAllRead (I := I) (E := E) vid pi dec.rdOps) ι T).1 with
  | none => exact ExecAvoids.ret _ _ _
  | some rdKeys =>
    obtain ⟨havWr, hafWr, hqsWr⟩ :=
      resolveAllWrite_local (I := I) (E := E) hpc hdec dec.wrOps (fun _ hy => hy) _
    apply ExecAvoids.bind_op havWr
    rw [hafWr]
    cases h2 : (exec (resolveAllWrite (I := I) (E := E) vid pi dec.wrOps) _ T).1 with
    | none => exact ExecAvoids.ret _ _ _
    | some wrKeys =>
      apply ExecAvoids.bind_op (readKeyList_local rdKeys (hpsRd rdKeys h1) _ T)
      apply ExecAvoids.bind_op (recvMany_local h.numInputs _ _)
      cases hev : h.eval wrKeys _ _ with
      | error _ => exact ExecAvoids.ret _ _ _
      | ok out =>
        obtain ⟨hwOut, _⟩ := hH1 wrKeys _ _ out hev
        apply ExecAvoids.bind_op (emitAll_local out.events _ _)
        apply ExecAvoids.bind_op
          (putAll_local out.writes (fun k v hm => hqsWr wrKeys h2 k (hwOut k v hm)) _ _)
        apply ExecAvoids.wr_bind
        · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
          exact avoid_of_mem (Or.inl (Or.inr (Or.inl rfl)))
        cases out.setStatus with
        | none => exact ExecAvoids.ret _ _ _
        | some s =>
          apply ExecAvoids.wr_bind
          · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
            exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inl rfl))))
          exact ExecAvoids.ret _ _ _

private theorem userCallRule_local {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hrdSafe : ∀ x ∈ dec.rdOps, ReadOpSafe vid pi T x)
    (hwrSafe : ∀ y ∈ dec.wrOps, WriteOpSafe vid pi T y) (ι : Nat → I) :
    ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (userCallRuleProg (I := I) (E := E) dec.op vid pi dec.rdOps dec.wrOps) ι T := by
  unfold userCallRuleProg
  have hSigLoc : StepCoreLocal vid T (.libSig dec.op) :=
    Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inr (Or.inr (Or.inr (Or.inl rfl))))⟩
  have hArgs : ∀ x ∈ dec.rdOps, x ∈ dec.rdOps ∧ ReadOpSafe vid pi T x :=
    fun x hx => ⟨hx, hrdSafe x hx⟩
  have hWrs : ∀ y ∈ dec.wrOps, y ∈ dec.wrOps ∧ WriteOpSafe vid pi T y :=
    fun y hy => ⟨hy, hwrSafe y hy⟩
  apply ExecAvoids.bind_op (bindChild_local hpc hdec hSigLoc hArgs hWrs ι)
  cases (exec (bindChild (I := I) (E := E) vid pi dec.op dec.rdOps dec.wrOps) ι T).1 with
  | error _ => exact ExecAvoids.ret _ _ _
  | ok u =>
    cases u
    apply ExecAvoids.wr_bind
    · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
      exact avoid_of_mem (Or.inl (Or.inr (Or.inl rfl)))
    exact ExecAvoids.ret _ _ _

private theorem ext_run_local {R : Runtime D I E} {ext : Extension D I E}
    (hExt : ExtensionSpec ext) {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hNotCore : isCoreOp dec.op = false) (hNotCap : R.caps dec.op = false)
    (ι : Nat → I) (S : Store Key (Val D)) :
    ExecAvoids
      (NotLocal (fun k => StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k))
      (NotLocal (fun k => StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k))
      (ext.run vid pi dec.rdOps dec.wrOps) ι S := by
  refine ⟨?_, ?_⟩
  · intro k v hm
    exact avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, hNotCore, hNotCap,
      hExt.readsSafe vid pi dec.rdOps dec.wrOps ι S k v hm⟩)
  · intro w hm
    have hmw := Act.mem_writes_of_mem hm
    rw [(hExt.internalPure vid pi dec.rdOps dec.wrOps ι S).1] at hmw
    cases hmw

private theorem delegRule_local {R : Runtime D I E} {ext : Extension D I E}
    (hExt : ExtensionSpec ext) {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hNotCore : isCoreOp dec.op = false) (hNotCap : R.caps dec.op = false)
    (ι : Nat → I) :
    ExecAvoids
      (NotLocal (fun k => StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k))
      (NotLocal (fun k => StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k))
      (delegRuleProg ext dec.op vid pi dec.rdOps dec.wrOps) ι T := by
  unfold delegRuleProg
  have hqLoc : StepCoreLocal vid T (.queueEntry dec.op vid) ∨ InExtensionH2Footprint R vid T (.queueEntry dec.op vid) :=
    Or.inl (Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inr (Or.inr (Or.inl rfl)))⟩)
  have hstLoc : StepCoreLocal vid T (.statusKey vid) ∨ InExtensionH2Footprint R vid T (.statusKey vid) :=
    Or.inl (Or.inl (Or.inr (Or.inr (Or.inl rfl))))
  have hpcLoc : StepCoreLocal vid T (.pcKey vid) ∨ InExtensionH2Footprint R vid T (.pcKey vid) :=
    Or.inl (Or.inl (Or.inr (Or.inl rfl)))
  apply ExecAvoids.rd_bind (avoid_of_mem hqLoc)
  cases T (.queueEntry dec.op vid) with
  | some _ => exact ExecAvoids.ret _ _ _
  | none =>
    apply ExecAvoids.wr_bind
    · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
      exact avoid_of_mem hstLoc
    apply ExecAvoids.wr_bind
    · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
      exact avoid_of_mem hqLoc
    apply ExecAvoids.bind_op (ext_run_local hExt hpc hdec hNotCore hNotCap _ _)
    cases hext : (exec (ext.run vid pi dec.rdOps dec.wrOps) _ _).1 with
    | error _ =>
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem hqLoc
      exact ExecAvoids.ret _ _ _
    | ok out =>
      apply ExecAvoids.bind_op (emitAll_local out.events _ _)
      apply ExecAvoids.bind_op
        (putAll_local out.writes (fun k v hm =>
          Or.inr ⟨pi, dec, hpc, hdec, hNotCore, hNotCap,
            (hExt.writesSafe vid pi dec.rdOps dec.wrOps _ _ out k v hext hm).1⟩) _ _)
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem hstLoc
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem hpcLoc
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem hqLoc
      exact ExecAvoids.ret _ _ _

private theorem handoffRule_local {R : Runtime D I E} {ext : Extension D I E}
    (hExt : ExtensionSpec ext) {vid : Vid} {pi : PC} {dec : DecodedInstr D}
    {T : Store Key (Val D)} (hpc : T (.pcKey vid) = some (.pc pi))
    (hdec : decodeInstrPure vid pi T = .ok dec)
    (hNotCore : isCoreOp dec.op = false) (hNotCap : R.caps dec.op = false)
    (ι : Nat → I) :
    ExecAvoids
      (NotLocal (fun k => StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k))
      (NotLocal (fun k => StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k))
      (handoffRuleProg ext dec.op vid pi dec.rdOps dec.wrOps) ι T := by
  unfold handoffRuleProg
  have hqLoc : StepCoreLocal vid T (.queueEntry dec.op vid) ∨ InExtensionH2Footprint R vid T (.queueEntry dec.op vid) :=
    Or.inl (Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inr (Or.inr (Or.inl rfl)))⟩)
  have hpcLoc : StepCoreLocal vid T (.pcKey vid) ∨ InExtensionH2Footprint R vid T (.pcKey vid) :=
    Or.inl (Or.inl (Or.inr (Or.inl rfl)))
  apply ExecAvoids.rd_bind (avoid_of_mem hqLoc)
  cases T (.queueEntry dec.op vid) with
  | some _ => exact ExecAvoids.ret _ _ _
  | none =>
    apply ExecAvoids.wr_bind
    · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
      exact avoid_of_mem hqLoc
    apply ExecAvoids.bind_op (ext_run_local hExt hpc hdec hNotCore hNotCap _ _)
    cases hext : (exec (ext.run vid pi dec.rdOps dec.wrOps) _ _).1 with
    | error _ =>
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem hqLoc
      exact ExecAvoids.ret _ _ _
    | ok out =>
      apply ExecAvoids.bind_op (emitAll_local out.events _ _)
      apply ExecAvoids.bind_op
        (putAll_local out.writes (fun k v hm =>
          Or.inr ⟨pi, dec, hpc, hdec, hNotCore, hNotCap,
            (hExt.writesSafe vid pi dec.rdOps dec.wrOps _ _ out k v hext hm).1⟩) _ _)
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem hpcLoc
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem hqLoc
      exact ExecAvoids.ret _ _ _

private theorem finalizeStep_local {P : Key → Prop} {vid : Vid}
    (hst : P (.statusKey vid)) (hmsg : P (.msgKey vid))
    (res : Except ErrMsg (PC × List E)) (ι : Nat → I) (S : Store Key (Val D)) :
    ExecAvoids (NotLocal P) (NotLocal P) (finalizeStep (D := D) vid res) ι S := by
  unfold finalizeStep
  cases res with
  | ok pair => rcases pair with ⟨_, _⟩; exact ExecAvoids.ret _ _ _
  | error _ =>
    apply ExecAvoids.wr_bind
    · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
      exact avoid_of_mem hmsg
    apply ExecAvoids.wr_bind
    · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
      exact avoid_of_mem hst
    exact ExecAvoids.ret _ _ _

/-- **Lemma `lem:locality` (strict non-extension form)**:
under `RuntimeSpec R`, `StepDiscipline vid T`, and `NotHandoffOrDeleg R vid T`,
every read `Act.read k v` and every write `Act.write w` (`w.affects k = true`)
in `(exec (gatedStep R vid) ι T).2` lies in `StepCoreLocal vid T k` (the exact
union of regions (i), (ii), and (iii) of Appendix A.3). -/
theorem gatedStep_locality_no_ext {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)}
    (hP : StepDiscipline vid T)
    (hNoExt : NotHandoffOrDeleg R vid T) (ι : Nat → I) :
    (∀ k v, Act.read k v ∈ (exec (gatedStep R vid) ι T).2 → StepCoreLocal vid T k) ∧
    (∀ w, Act.write w ∈ (exec (gatedStep R vid) ι T).2 →
      ∀ k, w.affects k = true → StepCoreLocal vid T k) := by
  have hav : ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T))
      (gatedStep R vid) ι T := by
    unfold gatedStep
    apply ExecAvoids.rd_bind (avoid_of_mem (Or.inl (Or.inl rfl)))
    cases T (.stopKey vid) with
    | some _ =>
      unfold pauseAfterGate
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem (Or.inl (Or.inr (Or.inr (Or.inl rfl))))
      exact ExecAvoids.ret _ _ _
    | none =>
      apply ExecAvoids.bind_op ?_
      · exact finalizeStep_local (Or.inl (Or.inr (Or.inr (Or.inl rfl))))
          (Or.inl (Or.inr (Or.inr (Or.inr (Or.inl rfl))))) _ _ _
      · unfold stepCore
        apply ExecAvoids.rd_bind (avoid_of_mem (Or.inl (Or.inr (Or.inl rfl))))
        cases hpc : T (.pcKey vid) with
        | none => exact ExecAvoids.ret _ _ _
        | some vPc =>
          cases vPc <;> try exact ExecAvoids.ret _ _ _
          rename_i pi
          apply ExecAvoids.bind_op (decodeInstr_local hpc _)
          rw [after_decodeInstr]
          cases hd : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) _ T).1 with
          | error _ => exact ExecAvoids.ret _ _ _
          | ok dec =>
            have hdec : decodeInstrPure vid pi T = .ok dec := by
              rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi _ T]; exact hd
            obtain ⟨hrdSafe, hwrSafe⟩ := hP pi dec hpc hdec
            unfold dispatchInstr
            by_cases h1 : (dec.op == "=") = true
            · simp only [h1, ite_true]; exact copyRule_local hpc hdec _
            · simp only [h1, Bool.false_eq_true, ite_false]
              by_cases h2 : (dec.op == "goto") = true
              · simp only [h2, ite_true]; exact gotoRule_local hpc hdec _
              · simp only [h2, Bool.false_eq_true, ite_false]
                by_cases h3 : (dec.op == "br") = true
                · simp only [h3, ite_true]; exact brRule_local hpc hdec _
                · simp only [h3, Bool.false_eq_true, ite_false]
                  by_cases h4 : (dec.op == "call") = true
                  · simp only [h4, ite_true]
                    exact callRule_local hpc hdec (beq_iff_eq.1 h4) hrdSafe hwrSafe _
                  · simp only [h4, Bool.false_eq_true, ite_false]
                    by_cases h5 : (dec.op == "return") = true
                    · simp only [h5, ite_true]; exact returnRule_local hpc _
                    · simp only [h5, Bool.false_eq_true, ite_false]
                      by_cases hCap : R.caps dec.op = true
                      · simp only [hCap, ite_true]
                        exact nativeRule_local (hR.nativeH1 dec.op hCap) hpc hdec _
                      · simp only [hCap, Bool.false_eq_true, ite_false]
                        have hNotCore : isCoreOp dec.op = false := by
                          simp [isCoreOp, h1, h2, h3, h4, h5]
                        have hNotCap : R.caps dec.op = false :=
                          Bool.eq_false_iff.2 hCap
                        obtain ⟨hNoDelG, hNoHandG⟩ := hNoExt pi dec hpc hdec
                        apply ExecAvoids.rd_bind
                          (avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inl rfl)⟩))
                        cases hvDel : T (.delegEntry dec.op) with
                        | none =>
                          have hNotDel : ∀ rt, T (.delegEntry dec.op) ≠ some (.delegTarget rt) := by
                            intro rt h; rw [hvDel] at h; cases h
                          apply ExecAvoids.rd_bind
                            (avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inr (Or.inl rfl))⟩))
                          cases hvHdr : T (.libHeader dec.op) with
                          | none => exact ExecAvoids.ret _ _ _
                          | some vHdr =>
                            cases vHdr with
                            | routeHdr rt =>
                              exact absurd (.handoff hNotCore hNotCap hNotDel ⟨rt, hvHdr⟩) hNoHandG
                            | rwfuncHdr => exact userCallRule_local hpc hdec hrdSafe hwrSafe _
                            | _ => exact ExecAvoids.ret _ _ _
                        | some vDel =>
                          cases vDel with
                          | delegTarget rt =>
                            exact absurd (.deleg hNotCore hNotCap ⟨rt, hvDel⟩) hNoDelG
                          | _ =>
                            have hNotDel : ∀ rt, T (.delegEntry dec.op) ≠ some (.delegTarget rt) := by
                              intro rt h; rw [hvDel] at h; cases h
                            apply ExecAvoids.rd_bind
                              (avoid_of_mem (Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inr (Or.inl rfl))⟩))
                            cases hvHdr : T (.libHeader dec.op) with
                            | none => exact ExecAvoids.ret _ _ _
                            | some vHdr =>
                              cases vHdr with
                              | routeHdr rt =>
                                exact absurd (.handoff hNotCore hNotCap hNotDel ⟨rt, hvHdr⟩) hNoHandG
                              | rwfuncHdr => exact userCallRule_local hpc hdec hrdSafe hwrSafe _
                              | _ => exact ExecAvoids.ret _ _ _
  exact ⟨fun k v hm => Classical.byContradiction (hav.1 k v hm),
         fun w hm k haff => Classical.byContradiction (hav.2 w hm k haff)⟩

/-- **Lemma `lem:locality` (full form across all 8 Figure 3 rules)**:
under `RuntimeSpec R` and `StepDiscipline vid T`, every read and write of
`(exec (gatedStep R vid) ι T).2` lies in `StepCoreLocal vid T k` or (when
dispatching `Deleg` or `Handoff`) in `InExtensionH2Footprint R vid T k`. -/
theorem gatedStep_locality {R : Runtime D I E} (hR : RuntimeSpec R)
    {vid : Vid} {T : Store Key (Val D)}
    (hP : StepDiscipline vid T) (ι : Nat → I) :
    (∀ k v, Act.read k v ∈ (exec (gatedStep R vid) ι T).2 →
      StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k) ∧
    (∀ w, Act.write w ∈ (exec (gatedStep R vid) ι T).2 →
      ∀ k, w.affects k = true →
      StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k) := by
  let Loc := fun k => StepCoreLocal vid T k ∨ InExtensionH2Footprint R vid T k
  have liftCore : ∀ {α : Type} {prog : Prog Key (Val D) I E α} {ι₀ : Nat → I} {S : Store Key (Val D)},
      ExecAvoids (NotLocal (StepCoreLocal vid T)) (NotLocal (StepCoreLocal vid T)) prog ι₀ S →
      ExecAvoids (NotLocal Loc) (NotLocal Loc) prog ι₀ S :=
    fun h => execAvoids_mono (fun _ hk => Or.inl hk) h
  have hav : ExecAvoids (NotLocal Loc) (NotLocal Loc) (gatedStep R vid) ι T := by
    unfold gatedStep
    apply ExecAvoids.rd_bind (avoid_of_mem (Or.inl (Or.inl (Or.inl rfl))))
    cases T (.stopKey vid) with
    | some _ =>
      unfold pauseAfterGate
      apply ExecAvoids.wr_bind
      · intro k haff; simp only [Write.affects, decide_eq_true_eq] at haff; subst haff
        exact avoid_of_mem (Or.inl (Or.inl (Or.inr (Or.inr (Or.inl rfl)))))
      exact ExecAvoids.ret _ _ _
    | none =>
      apply ExecAvoids.bind_op ?_
      · exact finalizeStep_local (Or.inl (Or.inl (Or.inr (Or.inr (Or.inl rfl)))))
          (Or.inl (Or.inl (Or.inr (Or.inr (Or.inr (Or.inl rfl)))))) _ _ _
      · unfold stepCore
        apply ExecAvoids.rd_bind (avoid_of_mem (Or.inl (Or.inl (Or.inr (Or.inl rfl)))))
        cases hpc : T (.pcKey vid) with
        | none => exact ExecAvoids.ret _ _ _
        | some vPc =>
          cases vPc <;> try exact ExecAvoids.ret _ _ _
          rename_i pi
          apply ExecAvoids.bind_op (liftCore (decodeInstr_local hpc _))
          rw [after_decodeInstr]
          cases hd : (exec (decodeInstr (D := D) (I := I) (E := E) vid pi) _ T).1 with
          | error _ => exact ExecAvoids.ret _ _ _
          | ok dec =>
            have hdec : decodeInstrPure vid pi T = .ok dec := by
              rw [← exec_decodeInstr_fst (I := I) (E := E) vid pi _ T]; exact hd
            obtain ⟨hrdSafe, hwrSafe⟩ := hP pi dec hpc hdec
            unfold dispatchInstr
            by_cases h1 : (dec.op == "=") = true
            · simp only [h1, ite_true]; exact liftCore (copyRule_local hpc hdec _)
            · simp only [h1, Bool.false_eq_true, ite_false]
              by_cases h2 : (dec.op == "goto") = true
              · simp only [h2, ite_true]; exact liftCore (gotoRule_local hpc hdec _)
              · simp only [h2, Bool.false_eq_true, ite_false]
                by_cases h3 : (dec.op == "br") = true
                · simp only [h3, ite_true]; exact liftCore (brRule_local hpc hdec _)
                · simp only [h3, Bool.false_eq_true, ite_false]
                  by_cases h4 : (dec.op == "call") = true
                  · simp only [h4, ite_true]
                    exact liftCore (callRule_local hpc hdec (beq_iff_eq.1 h4) hrdSafe hwrSafe _)
                  · simp only [h4, Bool.false_eq_true, ite_false]
                    by_cases h5 : (dec.op == "return") = true
                    · simp only [h5, ite_true]; exact liftCore (returnRule_local hpc _)
                    · simp only [h5, Bool.false_eq_true, ite_false]
                      by_cases hCap : R.caps dec.op = true
                      · simp only [hCap, ite_true]
                        exact liftCore (nativeRule_local (hR.nativeH1 dec.op hCap) hpc hdec _)
                      · simp only [hCap, Bool.false_eq_true, ite_false]
                        have hNotCore : isCoreOp dec.op = false := by
                          simp [isCoreOp, h1, h2, h3, h4, h5]
                        have hNotCap : R.caps dec.op = false :=
                          Bool.eq_false_iff.2 hCap
                        apply ExecAvoids.rd_bind
                          (avoid_of_mem (Or.inl (Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inl rfl)⟩)))
                        cases T (.delegEntry dec.op) with
                        | none =>
                          apply ExecAvoids.rd_bind
                            (avoid_of_mem (Or.inl (Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inr (Or.inl rfl))⟩)))
                          cases T (.libHeader dec.op) with
                          | none => exact ExecAvoids.ret _ _ _
                          | some vHdr =>
                            cases vHdr with
                            | routeHdr _ =>
                              exact handoffRule_local (hR.handoffH2 dec.op) hpc hdec hNotCore hNotCap _
                            | rwfuncHdr =>
                              exact liftCore (userCallRule_local hpc hdec hrdSafe hwrSafe _)
                            | _ => exact ExecAvoids.ret _ _ _
                        | some vDel =>
                          cases vDel with
                          | delegTarget _ =>
                            exact delegRule_local (hR.delegH2 dec.op) hpc hdec hNotCore hNotCap _
                          | _ =>
                            apply ExecAvoids.rd_bind
                              (avoid_of_mem (Or.inl (Or.inr ⟨pi, dec, hpc, hdec, Or.inr (Or.inr (Or.inl rfl))⟩)))
                            cases T (.libHeader dec.op) with
                            | none => exact ExecAvoids.ret _ _ _
                            | some vHdr =>
                              cases vHdr with
                              | routeHdr _ =>
                                exact handoffRule_local (hR.handoffH2 dec.op) hpc hdec hNotCore hNotCap _
                              | rwfuncHdr =>
                                exact liftCore (userCallRule_local hpc hdec hrdSafe hwrSafe _)
                              | _ => exact ExecAvoids.ret _ _ _
  exact ⟨fun k v hm => Classical.byContradiction (hav.1 k v hm),
         fun w hm k haff => Classical.byContradiction (hav.2 w hm k haff)⟩

end Locality

end Pathlang
