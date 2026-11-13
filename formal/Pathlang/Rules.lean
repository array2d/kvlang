import Pathlang.ProgLemmas

/-!
# Small-Step Operational Semantics as Step Programs (T1)

Mechanization of Figure 3, the v2 stop gate, the pause and failure transitions,
hypotheses `H1` and `H2` of Appendix A, rule guard exclusivity, single-step
determinism, and core invariance.
-/

namespace Pathlang

/-- Core opcodes fixed across all runtimes (`=`, `goto`, `br`, `call`, `return`). -/
def isCoreOp (op : SymName) : Bool :=
  op == "=" || op == "goto" || op == "br" || op == "call" || op == "return"

/-- Outcome of a single gated driver transition from a runnable store. -/
inductive StepOutcome (E : Type) where
  | paused
  | stepped (pcAfter : PC) (events : List E)
  | failed (err : ErrMsg)
  deriving DecidableEq, Repr

/-- Observable label emitted by a completed step (`<pc_{T'}, events>`). -/
structure StepLabel (E : Type) where
  pcAfter : PC
  events : List E
  deriving DecidableEq, Repr

namespace StepOutcome

def label? {E : Type} : StepOutcome E → Option (StepLabel E)
  | .stepped pi evs => some ⟨pi, evs⟩
  | .paused => none
  | .failed _ => none

end StepOutcome

/-! ## Parametric Native Handlers (`H1`) and Extensions (`H2`) -/

/-- Output of a native handler `[[op]]`. -/
structure NativeOut (D E : Type) where
  writes : List (Key × Val D)
  events : List E
  setStatus : Option Status

/-- Parametric native handler (`Native` rule). -/
structure NativeHandler (D I E : Type) where
  numInputs : Nat
  eval : List Key → List (Option (Val D)) → List I → Except ErrMsg (NativeOut D E)

/-- Hypothesis **H1** (Appendix A.1): a native handler is a pure function of its
resolved write keys, resolved read values, and external inputs (enforced by the
type of `eval`), writes only its resolved write paths, is atomic on failure
(returning `.error` applies zero writes), and any status it sets is not `error`. -/
def HandlerSpec {D I E : Type} (h : NativeHandler D I E) : Prop :=
  ∀ wrKeys rdVals inputs out,
    h.eval wrKeys rdVals inputs = .ok out →
      (∀ k v, (k, v) ∈ out.writes → k ∈ wrKeys) ∧
      (∀ s, out.setStatus = some s → s ≠ .error)

/-- Output of a delegated or routed extension run `X_{op}`. -/
structure ExtensionOut (D E : Type) where
  events : List E
  writes : List (Key × Val D)
  nextPc : PC

/-- Parametric extension serving `Handoff` or `Deleg`. -/
structure Extension (D I E : Type) where
  run : Vid → PC → List (Key × Val D) → List (Key × Val D) →
    Prog Key (Val D) I E (Except ErrMsg (ExtensionOut D E))

/-- Keys forbidden for operand resolution and extension reads at `(vid, d)`:
dead frames of `vid`, thread control keys (`kappa`, `sigma`, `rho`, `lambda`,
`mu`), frame system keys (`..lib`, `..callpc`, `..returnpc`), and queue entries. -/
def ForbiddenKey (vid : Vid) (d : Nat) (k : Key) : Bool :=
  isDeadFrameKey vid d k || isThreadControlKey vid k

/-- Hypothesis **H2** (Appendix A.1, refined with frame preservation per Finding 4):
an extension does not consult the gate (`rho`) or other control/dead-frame keys
while holding the thread, stages its effect atomically (its internal computation
performs no direct store writes or events; on `.error`, zero extension writes are
applied), writes only non-forbidden non-parameter paths on success, and returns a
valid distinct counter `nextPc` in the same frame (`nextPc.depth = pi.depth`). -/
structure ExtensionSpec {D I E : Type} (ext : Extension D I E) : Prop where
  internalPure :
    ∀ vid pi rdOps wrOps ι T,
      Act.writes (exec (ext.run vid pi rdOps wrOps) ι T).2 = [] ∧
      Act.events (exec (ext.run vid pi rdOps wrOps) ι T).2 = []
  readsSafe :
    ∀ vid pi rdOps wrOps ι T k v,
      Act.read k v ∈ (exec (ext.run vid pi rdOps wrOps) ι T).2 →
      ForbiddenKey vid pi.depth k = false
  writesSafe :
    ∀ vid pi rdOps wrOps ι T out k v,
      (exec (ext.run vid pi rdOps wrOps) ι T).1 = .ok out →
      (k, v) ∈ out.writes →
      ForbiddenKey vid pi.depth k = false ∧ isFrameParamKey vid k = false
  pcValid :
    ∀ vid pi rdOps wrOps ι T out,
      pi.valid = true →
      (exec (ext.run vid pi rdOps wrOps) ι T).1 = .ok out →
      out.nextPc ≠ pi ∧ out.nextPc.valid = true ∧ out.nextPc.depth = pi.depth

/-- Running runtime configuration `R`. -/
structure Runtime (D I E : Type) where
  caps : SymName → Bool
  native : SymName → NativeHandler D I E
  delegExt : SymName → Extension D I E
  handoffExt : SymName → Extension D I E

/-- Combined runtime specification: `caps(R)` excludes `Core`, every native
handler in `caps(R)` satisfies `H1`, and every delegated or routed extension
satisfies `H2`. -/
structure RuntimeSpec {D I E : Type} (R : Runtime D I E) : Prop where
  capsDisjointCore : ∀ op, isCoreOp op = true → R.caps op = false
  nativeH1 : ∀ op, R.caps op = true → HandlerSpec (R.native op)
  delegH2 : ∀ op, ExtensionSpec (R.delegExt op)
  handoffH2 : ∀ op, ExtensionSpec (R.handoffExt op)

/-! ## Auxiliary Step Programs (§3.2) -/

section AuxProgs

variable {D I E : Type}

/-- Follow `hops` pointer links starting at `k`. -/
def derefChain : Nat → Key → Prog Key (Val D) I E (Option Key)
  | 0, k => pure (some k)
  | hops + 1, k => do
      let v ← Prog.rd k
      match v with
      | some (.ptr k') => derefChain hops k'
      | _ => pure none

/-- Resolve a read operand `(slotKey, cellVal)`: references follow `hops`
pointer links from their base slot; inline literals are read in place at
`slotKey`. -/
def resolveRead (vid : Vid) (pi : PC) (opCell : Key × Val D) :
    Prog Key (Val D) I E (Option Key) :=
  match opCell.2 with
  | .ref hops base => derefChain hops (base.startKey vid pi.depth)
  | _ => pure (some opCell.1)

/-- Resolve a write operand `(slotKey, cellVal)`: must be a reference, never a
literal. -/
def resolveWrite (vid : Vid) (pi : PC) (opCell : Key × Val D) :
    Prog Key (Val D) I E (Option Key) :=
  match opCell.2 with
  | .ref hops base => derefChain hops (base.startKey vid pi.depth)
  | _ => pure none

def resolveAllRead (vid : Vid) (pi : PC) :
    List (Key × Val D) → Prog Key (Val D) I E (Option (List Key))
  | [] => pure (some [])
  | x :: xs => do
      let p? ← resolveRead vid pi x
      match p? with
      | none => pure none
      | some p =>
          let ps? ← resolveAllRead vid pi xs
          match ps? with
          | none => pure none
          | some ps => pure (some (p :: ps))

def resolveAllWrite (vid : Vid) (pi : PC) :
    List (Key × Val D) → Prog Key (Val D) I E (Option (List Key))
  | [] => pure (some [])
  | x :: xs => do
      let q? ← resolveWrite vid pi x
      match q? with
      | none => pure none
      | some q =>
          let qs? ← resolveAllWrite vid pi xs
          match qs? with
          | none => pure none
          | some qs => pure (some (q :: qs))

/-- Resolve `x` and read a positive instruction row target (`>= 1`). -/
def resolveRowTarget (vid : Vid) (pi : PC) (opCell : Key × Val D) :
    Prog Key (Val D) I E (Option Nat) := do
  let p? ← resolveRead vid pi opCell
  match p? with
  | none => pure none
  | some p =>
      let v ← Prog.rd p
      match v with
      | some (.int (.ofNat (n + 1))) => pure (some (n + 1))
      | _ => pure none

/-- Resolve `x` and read a boolean condition (`truth(x)`). -/
def resolveTruth (vid : Vid) (pi : PC) (opCell : Key × Val D) :
    Prog Key (Val D) I E (Option Bool) := do
  let p? ← resolveRead vid pi opCell
  match p? with
  | none => pure none
  | some p =>
      let v ← Prog.rd p
      match v with
      | some (.bool b) => pure (some b)
      | _ => pure none

/-- Read operand slots `start .. start + count - 1` along `dir` of `(fn, row)`. -/
def readOperandsFrom (fn : SymName) (row : Nat) (dir : ParamDir) :
    Nat → Nat → Prog Key (Val D) I E (Option (List (Key × Val D)))
  | _, 0 => pure (some [])
  | idx, count + 1 => do
      let k := Key.libOperand fn row dir idx
      let v? ← Prog.rd k
      match v? with
      | none => pure none
      | some v =>
          let rest? ← readOperandsFrom fn row dir (idx + 1) count
          match rest? with
          | none => pure none
          | some rest => pure (some ((k, v) :: rest))

/-- Decoded instruction at `pi`. -/
structure DecodedInstr (D : Type) where
  fn : SymName
  op : SymName
  rdOps : List (Key × Val D)
  wrOps : List (Key × Val D)

/-- `decode(pi)`: read `..lib` of the current frame, read `[s0, 0]`, and read
all `nr` read and `nw` write operand cells. -/
def decodeInstr (vid : Vid) (pi : PC) :
    Prog Key (Val D) I E (Except ErrMsg (DecodedInstr D)) := do
  if !pi.valid then
    pure (.error "invalid_pc")
  else
    let vLib ← Prog.rd (.frameLib vid pi.depth)
    match vLib with
    | some (.sym fn) =>
        let vIns ← Prog.rd (.libInstr fn pi.row)
        match vIns with
        | some (.instr op nr nw) =>
            let rds? ← readOperandsFrom fn pi.row .rd 1 nr
            match rds? with
            | none => pure (.error "missing_rd_operand")
            | some rdOps =>
                let wrs? ← readOperandsFrom fn pi.row .wr 1 nw
                match wrs? with
                | none => pure (.error "missing_wr_operand")
                | some wrOps => pure (.ok ⟨fn, op, rdOps, wrOps⟩)
        | _ => pure (.error "bad_opcode")
    | _ => pure (.error "missing_frame_lib")

/-- Write parameter binding pointers `[dChild]/[0, +-j] := ptr target` from index `idx`. -/
def writeParamPtrs (vid : Vid) (dChild : Nat) (dir : ParamDir) :
    Nat → List Key → Prog Key (Val D) I E Unit
  | _, [] => pure ()
  | idx, k :: ks => do
      Prog.wr (.put (.frameParam vid dChild dir idx) (.ptr k))
      writeParamPtrs vid dChild dir (idx + 1) ks

/-- Resolve each read actual argument and bind its pointer into the child frame. -/
def bindReadArgs (vid : Vid) (pi : PC) :
    Nat → List (Key × Val D) → Prog Key (Val D) I E (Except ErrMsg Unit)
  | _, [] => pure (.ok ())
  | idx, x :: xs => do
      let p? ← resolveRead vid pi x
      match p? with
      | none => pure (.error "bad_call_rd_arg")
      | some p =>
          Prog.wr (.put (.frameParam vid (pi.depth + 1) .rd idx) (.ptr p))
          bindReadArgs vid pi (idx + 1) xs

/-- Resolve each write actual argument and bind its pointer into the child frame. -/
def bindWriteArgs (vid : Vid) (pi : PC) :
    Nat → List (Key × Val D) → Prog Key (Val D) I E (Except ErrMsg Unit)
  | _, [] => pure (.ok ())
  | idx, x :: xs => do
      let q? ← resolveWrite vid pi x
      match q? with
      | none => pure (.error "bad_call_wr_arg")
      | some q =>
          Prog.wr (.put (.frameParam vid (pi.depth + 1) .wr idx) (.ptr q))
          bindWriteArgs vid pi (idx + 1) xs

/-- `bind(pi, callee, rdArgs, wrArgs)` (§3.2): read signature `[0,0]` of `callee`,
`delsub(child(pi))`, write child `..lib`, `..returnpc`, `..callpc`, and bind all
read and write parameter pointers. -/
def bindChild (vid : Vid) (pi : PC) (callee : SymName)
    (rdArgs wrArgs : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg Unit) := do
  let vSig ← Prog.rd (.libSig callee)
  match vSig with
  | some (.sig sigNr sigNw) =>
      if sigNr != rdArgs.length || sigNw != wrArgs.length then
        pure (.error "arity_mismatch")
      else
        let dChild := pi.depth + 1
        Prog.wr (.delWhere (inFrame vid dChild))
        Prog.wr (.put (.frameLib vid dChild) (.sym callee))
        Prog.wr (.put (.frameRetPc vid dChild) (.pc pi.next))
        Prog.wr (.put (.frameCallPc vid dChild) (.pc pi.entry))
        let r1 ← bindReadArgs vid pi 1 rdArgs
        match r1 with
        | .error err => pure (.error err)
        | .ok () => bindWriteArgs vid pi 1 wrArgs
  | _ => pure (.error "missing_callee_sig")

/-- Read values at a list of keys in order. -/
def readKeyList : List Key → Prog Key (Val D) I E (List (Option (Val D)))
  | [] => pure []
  | k :: ks => do
      let v ← Prog.rd k
      let vs ← readKeyList ks
      pure (v :: vs)

/-- Consume `n` external inputs in order. -/
def recvMany : Nat → Prog Key (Val D) I E (List I)
  | 0 => pure []
  | n + 1 => do
      let i ← Prog.recv
      let is ← recvMany n
      pure (i :: is)

/-- Emit a list of events in order. -/
def emitAll : List E → Prog Key (Val D) I E Unit
  | [] => pure ()
  | e :: es => do
      Prog.out e
      emitAll es

/-- Apply a list of `(key, val)` atomic `put` writes in order. -/
def putAll : List (Key × Val D) → Prog Key (Val D) I E Unit
  | [] => pure ()
  | (k, v) :: kvs => do
      Prog.wr (.put k v)
      putAll kvs

/-! ## The Small-Step Rules of Figure 3 as Step Programs -/

/-- **Rule `Copy`** (`op = "="`). -/
def copyRuleProg (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) :=
  match rdOps, wrOps with
  | [rd1], [wr1] => do
      let p? ← resolveRead vid pi rd1
      match p? with
      | none => pure (.error "copy_bad_src")
      | some p =>
          let q? ← resolveWrite vid pi wr1
          match q? with
          | none => pure (.error "copy_bad_dst")
          | some q =>
              let v? ← Prog.rd p
              match v? with
              | none => pure (.error "copy_missing_val")
              | some val =>
                  Prog.wr (.put q val)
                  Prog.wr (.put (.pcKey vid) (.pc pi.next))
                  pure (.ok (pi.next, []))
  | _, _ => pure (.error "copy_arity")

/-- **Rule `Goto`** (`op = "goto"`). -/
def gotoRuleProg (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) :=
  match rdOps, wrOps with
  | [rd1], [] => do
      let n? ← resolveRowTarget vid pi rd1
      match n? with
      | none => pure (.error "goto_bad_target")
      | some n =>
          Prog.wr (.put (.pcKey vid) (.pc (pi.target n)))
          pure (.ok (pi.target n, []))
  | _, _ => pure (.error "goto_arity")

/-- **Rule `Br`** (`op = "br"`). -/
def brRuleProg (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) :=
  match rdOps, wrOps with
  | [rd1, rd2, rd3], [] => do
      let b? ← resolveTruth vid pi rd1
      match b? with
      | none => pure (.error "br_bad_cond")
      | some b =>
          let n? ← resolveRowTarget vid pi (if b then rd2 else rd3)
          match n? with
          | none => pure (.error "br_bad_target")
          | some n =>
              Prog.wr (.put (.pcKey vid) (.pc (pi.target n)))
              pure (.ok (pi.target n, []))
  | _, _ => pure (.error "br_arity")

/-- **Rule `Call`** (`op = "call"`). -/
def callRuleProg (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) :=
  match rdOps with
  | fnOp :: argRdOps => do
      let pFn? ← resolveRead vid pi fnOp
      match pFn? with
      | none => pure (.error "call_bad_fn_ref")
      | some pFn =>
          let vFn ← Prog.rd pFn
          match vFn with
          | some (.sym callee) =>
              let bRes ← bindChild vid pi callee argRdOps wrOps
              match bRes with
              | .error err => pure (.error err)
              | .ok () =>
                  Prog.wr (.put (.pcKey vid) (.pc pi.entry))
                  pure (.ok (pi.entry, []))
          | _ => pure (.error "call_bad_fn_sym")
  | [] => pure (.error "call_arity")

/-- **Rule `Return`** (`op = "return"`): pops to parent counter before `delsub`
when `depth > 1`, or commits terminal `ok` at root depth `1`. -/
def returnRuleProg (vid : Vid) (pi : PC) (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) :=
  match rdOps, wrOps with
  | [], [] =>
      if 1 < pi.depth then do
        let vRet ← Prog.rd (.frameRetPc vid pi.depth)
        match vRet with
        | some (.pc r) =>
            if r.valid && decide (r.depth + 1 = pi.depth) then do
              Prog.wr (.put (.pcKey vid) (.pc r))
              Prog.wr (.delWhere (inFrame vid pi.depth))
              pure (.ok (r, []))
            else
              pure (.error "broken_return_chain")
        | _ => pure (.error "missing_returnpc")
      else do
        Prog.wr (.put (.statusKey vid) (.status .ok))
        pure (.ok (pi, []))
  | _, _ => pure (.error "return_arity")

/-- **Rule `Native`** (`op ∉ Core`, `op ∈ caps(R)`). -/
def nativeRuleProg (h : NativeHandler D I E) (vid : Vid) (pi : PC)
    (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) := do
  let rdKeys? ← resolveAllRead vid pi rdOps
  match rdKeys? with
  | none => pure (.error "native_bad_rd")
  | some rdKeys =>
      let wrKeys? ← resolveAllWrite vid pi wrOps
      match wrKeys? with
      | none => pure (.error "native_bad_wr")
      | some wrKeys =>
          let rdVals ← readKeyList rdKeys
          let inputs ← recvMany h.numInputs
          match h.eval wrKeys rdVals inputs with
          | .error err => pure (.error err)
          | .ok out =>
              emitAll out.events
              putAll out.writes
              Prog.wr (.put (.pcKey vid) (.pc pi.next))
              match out.setStatus with
              | none => pure (.ok (pi.next, out.events))
              | some s =>
                  Prog.wr (.put (.statusKey vid) (.status s))
                  pure (.ok (pi.next, out.events))

/-- **Rule `Deleg`** (`op ∉ Core`, `op ∉ caps(R)`, delegation entry present):
sets `sigma := wait`, records queue entry, runs extension, sets `sigma := running`,
commits `nextPc`, and removes queue entry (on failure removes queue entry and fails). -/
def delegRuleProg (ext : Extension D I E) (op : SymName) (vid : Vid) (pi : PC)
    (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) := do
  let q := Key.queueEntry op vid
  let vQ ← Prog.rd q
  match vQ with
  | some _ => pure (.error "stale_queue_entry")
  | none =>
      Prog.wr (.put (.statusKey vid) (.status .wait))
      Prog.wr (.put q (.pc pi))
      let res ← ext.run vid pi rdOps wrOps
      match res with
      | .error err =>
          Prog.wr (.del q)
          pure (.error err)
      | .ok out =>
          emitAll out.events
          putAll out.writes
          Prog.wr (.put (.statusKey vid) (.status .running))
          Prog.wr (.put (.pcKey vid) (.pc out.nextPc))
          Prog.wr (.del q)
          pure (.ok (out.nextPc, out.events))

/-- **Rule `Handoff`** (`op ∉ Core`, `op ∉ caps(R)`, not delegated, route header present). -/
def handoffRuleProg (ext : Extension D I E) (op : SymName) (vid : Vid) (pi : PC)
    (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) := do
  let q := Key.queueEntry op vid
  let vQ ← Prog.rd q
  match vQ with
  | some _ => pure (.error "stale_queue_entry")
  | none =>
      Prog.wr (.put q (.pc pi))
      let res ← ext.run vid pi rdOps wrOps
      match res with
      | .error err =>
          Prog.wr (.del q)
          pure (.error err)
      | .ok out =>
          emitAll out.events
          putAll out.writes
          Prog.wr (.put (.pcKey vid) (.pc out.nextPc))
          Prog.wr (.del q)
          pure (.ok (out.nextPc, out.events))

/-- **Rule `UserCall`** (`op ∉ Core`, `op ∉ caps(R)`, unrouted `rwfunc`):
as `Call` with `callee = op`. -/
def userCallRuleProg (op : SymName) (vid : Vid) (pi : PC)
    (rdOps wrOps : List (Key × Val D)) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) := do
  let bRes ← bindChild vid pi op rdOps wrOps
  match bRes with
  | .error err => pure (.error err)
  | .ok () =>
      Prog.wr (.put (.pcKey vid) (.pc pi.entry))
      pure (.ok (pi.entry, []))

/-- Dispatch a decoded instruction `(op, rdOps, wrOps)` across the Figure 3 rules. -/
def dispatchInstr (R : Runtime D I E) (vid : Vid) (pi : PC) (dec : DecodedInstr D) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) :=
  if dec.op == "=" then
    copyRuleProg vid pi dec.rdOps dec.wrOps
  else if dec.op == "goto" then
    gotoRuleProg vid pi dec.rdOps dec.wrOps
  else if dec.op == "br" then
    brRuleProg vid pi dec.rdOps dec.wrOps
  else if dec.op == "call" then
    callRuleProg vid pi dec.rdOps dec.wrOps
  else if dec.op == "return" then
    returnRuleProg vid pi dec.rdOps dec.wrOps
  else if R.caps dec.op then
    nativeRuleProg (R.native dec.op) vid pi dec.rdOps dec.wrOps
  else do
    let vDel ← Prog.rd (.delegEntry dec.op)
    match vDel with
    | some (.delegTarget _) =>
        delegRuleProg (R.delegExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
    | _ =>
        let vHdr ← Prog.rd (.libHeader dec.op)
        match vHdr with
        | some (.routeHdr _) =>
            handoffRuleProg (R.handoffExt dec.op) dec.op vid pi dec.rdOps dec.wrOps
        | some .rwfuncHdr =>
            userCallRuleProg dec.op vid pi dec.rdOps dec.wrOps
        | _ => pure (.error "unmatched_opcode")

/-- Step body after passing the `rd(rho) = none` gate: read `kappa`, decode `pi`,
and dispatch the matching rule. -/
def stepCore (R : Runtime D I E) (vid : Vid) :
    Prog Key (Val D) I E (Except ErrMsg (PC × List E)) := do
  let vPc ← Prog.rd (.pcKey vid)
  match vPc with
  | some (.pc pi) =>
      let dRes ← decodeInstr vid pi
      match dRes with
      | .error err => pure (.error err)
      | .ok dec => dispatchInstr R vid pi dec
  | _ => pure (.error "missing_pc")

/-- Driver completion of a step attempt: on failure, write `mu` and `sigma := error`. -/
def finalizeStep (vid : Vid) :
    Except ErrMsg (PC × List E) → Prog Key (Val D) I E (StepOutcome E)
  | .ok (pi', evs) => pure (.stepped pi' evs)
  | .error err => do
      Prog.wr (.put (.msgKey vid) (.errMsg err))
      Prog.wr (.put (.statusKey vid) (.status .error))
      pure (.failed err)

/-- Pause action after seeing `T(rho)` present at the gate (`put(sigma, paused)`). -/
def pauseAfterGate (vid : Vid) : Prog Key (Val D) I E (StepOutcome E) := do
  Prog.wr (.put (.statusKey vid) (.status .paused))
  pure .paused

/-- Complete v2 gated transition program (Model):
starts with `rd(rho)`; if present, pauses via `put(sigma, paused)`; if absent,
executes one step (or fails to `error`). -/
def gatedStep (R : Runtime D I E) (vid : Vid) :
    Prog Key (Val D) I E (StepOutcome E) := do
  let vRho ← Prog.rd (.stopKey vid)
  match vRho with
  | some _ => pauseAfterGate vid
  | none =>
      let res ← stepCore R vid
      finalizeStep vid res

end AuxProgs

/-! ## Rule Guards, Relational Small-Step Judgment, and Determinism -/

/-- Enumeration of all small-step rules. -/
inductive RuleTag where
  | copy
  | goto
  | br
  | call
  | returnSub
  | returnRoot
  | native
  | deleg
  | handoff
  | userCall
  deriving DecidableEq, Repr

/-- Relational guard selecting which rule applies to decoded opcode `op` at `pi` in store `T`. -/
inductive RuleGuard {D I E : Type} (R : Runtime D I E) (T : Store Key (Val D))
    (op : SymName) (pi : PC) : RuleTag → Prop where
  | copy : op = "=" → RuleGuard R T op pi .copy
  | goto : op = "goto" → RuleGuard R T op pi .goto
  | br : op = "br" → RuleGuard R T op pi .br
  | call : op = "call" → RuleGuard R T op pi .call
  | returnSub : op = "return" → 1 < pi.depth → RuleGuard R T op pi .returnSub
  | returnRoot : op = "return" → ¬ (1 < pi.depth) → RuleGuard R T op pi .returnRoot
  | native : isCoreOp op = false → R.caps op = true → RuleGuard R T op pi .native
  | deleg : isCoreOp op = false → R.caps op = false →
      (∃ rt, T (.delegEntry op) = some (.delegTarget rt)) →
      RuleGuard R T op pi .deleg
  | handoff : isCoreOp op = false → R.caps op = false →
      (∀ rt, T (.delegEntry op) ≠ some (.delegTarget rt)) →
      (∃ rt, T (.libHeader op) = some (.routeHdr rt)) →
      RuleGuard R T op pi .handoff
  | userCall : isCoreOp op = false → R.caps op = false →
      (∀ rt, T (.delegEntry op) ≠ some (.delegTarget rt)) →
      T (.libHeader op) = some .rwfuncHdr →
      RuleGuard R T op pi .userCall

/-- **Pairwise exclusivity of rule guards**: at most one rule matches any `(op, pi, T)`. -/
theorem ruleGuard_unique {D I E : Type} {R : Runtime D I E} {T : Store Key (Val D)}
    {op : SymName} {pi : PC} {r₁ r₂ : RuleTag}
    (h₁ : RuleGuard R T op pi r₁) (h₂ : RuleGuard R T op pi r₂) : r₁ = r₂ := by
  cases h₁ <;> cases h₂ <;> simp_all [isCoreOp]

/-- Configuration status guard: `T(sigma) ∈ Run = {init, running, wait}`. -/
def StoreRunnable {D : Type} (vid : Vid) (T : Store Key (Val D)) : Prop :=
  ∃ s, T (.statusKey vid) = some (.status s) ∧ s.isRun = true

/-- Relational v2 small-step transition `<T, vid> -> <T', vid>` with outcome and
action sequence, separating the pause transition, a successful step, and a
failing step. -/
inductive GatedRel {D I E : Type} (R : Runtime D I E) (vid : Vid) (ι : Nat → I)
    (T : Store Key (Val D)) :
    StepOutcome E → List (Act Key (Val D) I E) → Store Key (Val D) → Prop where
  | pause (v : Val D) :
      StoreRunnable vid T →
      T (.stopKey vid) = some v →
      GatedRel R vid ι T .paused
        [.read (.stopKey vid) (some v),
         .write (.put (.statusKey vid) (.status .paused))]
        (Write.apply (.put (.statusKey vid) (.status .paused)) T)
  | step (pi' : PC) (evs : List E) (asCore : List (Act Key (Val D) I E)) :
      StoreRunnable vid T →
      T (.stopKey vid) = none →
      exec (stepCore R vid) ι T = (.ok (pi', evs), asCore) →
      GatedRel R vid ι T (.stepped pi' evs)
        (.read (.stopKey vid) none :: asCore)
        (applyAll T (Act.writes asCore))
  | fail (err : ErrMsg) (asCore : List (Act Key (Val D) I E)) :
      StoreRunnable vid T →
      T (.stopKey vid) = none →
      exec (stepCore R vid) ι T = (.error err, asCore) →
      GatedRel R vid ι T (.failed err)
        (.read (.stopKey vid) none ::
          (asCore ++ [.write (.put (.msgKey vid) (.errMsg err)),
                      .write (.put (.statusKey vid) (.status .error))]))
        (applyAll T (Act.writes asCore ++
          [.put (.msgKey vid) (.errMsg err), .put (.statusKey vid) (.status .error)]))

/-- `GatedRel` coincides with executing `gatedStep R vid` from a runnable store. -/
theorem gatedRel_iff_exec {D I E : Type} (R : Runtime D I E) (vid : Vid)
    (ι : Nat → I) (T : Store Key (Val D))
    (out : StepOutcome E) (as : List (Act Key (Val D) I E)) (T' : Store Key (Val D)) :
    GatedRel R vid ι T out as T' ↔
      (StoreRunnable vid T ∧ exec (gatedStep R vid) ι T = (out, as) ∧
       after (gatedStep R vid) ι T = T') := by
  constructor
  · intro h
    cases h with
    | pause v hrun hrho =>
      refine ⟨hrun, ?_, ?_⟩
      · simp [gatedStep, pauseAfterGate, exec, hrho]
      · simp [gatedStep, pauseAfterGate, after, exec, hrho]
    | step pi' evs asCore hrun hrho hcore =>
      refine ⟨hrun, ?_, ?_⟩
      · simp [gatedStep, hrho, exec_bind, hcore, finalizeStep]
      · simp [gatedStep, after, hrho, exec_bind, hcore, finalizeStep]
    | fail err asCore hrun hrho hcore =>
      refine ⟨hrun, ?_, ?_⟩
      · simp [gatedStep, exec, hrho, exec_bind, hcore, finalizeStep]
      · simp [gatedStep, after, exec, hrho, exec_bind, hcore, finalizeStep, Act.writes_append]
  · rintro ⟨hrun, hexec, hafter⟩
    subst hafter
    cases hrho : T (.stopKey vid) with
    | some v =>
      have hex : exec (gatedStep R vid) ι T =
          (.paused, [.read (.stopKey vid) (some v),
                     .write (.put (.statusKey vid) (.status .paused))]) := by
        simp [gatedStep, pauseAfterGate, exec, hrho]
      rw [hex] at hexec
      rcases Prod.mk.inj hexec with ⟨rfl, rfl⟩
      simpa [gatedStep, pauseAfterGate, after, exec, hrho] using GatedRel.pause v hrun hrho
    | none =>
      cases hcore : exec (stepCore R vid) ι T with
      | mk res asCore =>
        cases res with
        | ok pair =>
          rcases pair with ⟨pi', evs⟩
          have hex : exec (gatedStep R vid) ι T =
              (.stepped pi' evs, .read (.stopKey vid) none :: asCore) := by
            simp [gatedStep, hrho, exec_bind, hcore, finalizeStep]
          rw [hex] at hexec
          rcases Prod.mk.inj hexec with ⟨rfl, rfl⟩
          simpa [gatedStep, after, exec, hrho, exec_bind, hcore, finalizeStep] using
            GatedRel.step pi' evs asCore hrun hrho hcore
        | error err =>
          have hex : exec (gatedStep R vid) ι T =
              (.failed err, .read (.stopKey vid) none ::
                (asCore ++ [.write (.put (.msgKey vid) (.errMsg err)),
                            .write (.put (.statusKey vid) (.status .error))])) := by
            simp [gatedStep, exec, hrho, exec_bind, hcore, finalizeStep]
          rw [hex] at hexec
          rcases Prod.mk.inj hexec with ⟨rfl, rfl⟩
          simpa [gatedStep, after, exec, hrho, exec_bind, hcore, finalizeStep, Act.writes_append]
            using GatedRel.fail err asCore hrun hrho hcore

/-- **Lemma 9 (`lem:determinism`) / Determinism (T1)**: from any configuration `<T, vid>`, given
external inputs `ι`, the outcome (pause, step with label, or failure), the
ordered action trace, and the successor store `T'` are unique. -/
theorem gatedRel_deterministic {D I E : Type} {R : Runtime D I E} {vid : Vid}
    {ι : Nat → I} {T T₁ T₂ : Store Key (Val D)}
    {out₁ out₂ : StepOutcome E} {as₁ as₂ : List (Act Key (Val D) I E)}
    (h₁ : GatedRel R vid ι T out₁ as₁ T₁)
    (h₂ : GatedRel R vid ι T out₂ as₂ T₂) :
    out₁ = out₂ ∧ as₁ = as₂ ∧ T₁ = T₂ := by
  obtain ⟨_, hex₁, haf₁⟩ := (gatedRel_iff_exec R vid ι T out₁ as₁ T₁).1 h₁
  obtain ⟨_, hex₂, haf₂⟩ := (gatedRel_iff_exec R vid ι T out₂ as₂ T₂).1 h₂
  rw [hex₁] at hex₂
  rcases Prod.mk.inj hex₂ with ⟨rfl, rfl⟩
  exact ⟨rfl, rfl, haf₁.symm.trans haf₂⟩

/-! ## Proposition 7: Core Invariance under Extension Installation (`(X)`) -/

/-- Assumption **(X)** (Appendix A.1): two stores `T₁` and `T₂` differ only by
installing or removing extensions on operations `extOps` (`isOpLibOrDelegKey`). -/
def ExtensionChangeOnly {D : Type} (extOps : SymName → Bool)
    (T₁ T₂ : Store Key (Val D)) : Prop :=
  ∀ k, (∀ op, extOps op = true → isOpLibOrDelegKey op k = false) → T₁ k = T₂ k

/-- **Proposition 7 (Core Invariance)**: if `T₁` and `T₂` differ only on
extension keys `extOps`, and the step from `T₁` exposes no key in `extOps` (as
holds for every core instruction in a user function whose operands and callee do
not touch `extOps`), then `stepCore` performs the exact same actions and writes
from `T₂` regardless of changes to `delegExt` or `handoffExt`. -/
theorem core_invariance {D I E : Type} (R₁ R₂ : Runtime D I E) (vid : Vid)
    (extOps : SymName → Bool) (ι : Nat → I) (T₁ T₂ : Store Key (Val D))
    (pi : PC) (dec : DecodedInstr D)
    (hCore : isCoreOp dec.op = true)
    (hpc₁ : T₁ (.pcKey vid) = some (.pc pi))
    (hdec₁ : (exec (decodeInstr (E := E) vid pi) ι T₁).1 = .ok dec)
    (hExt : ExtensionChangeOnly extOps T₁ T₂)
    (hUnexposed : ∀ k, Act.exposed (exec (stepCore R₁ vid) ι T₁).2 k = true →
      ∀ op, extOps op = true → isOpLibOrDelegKey op k = false) :
    exec (stepCore R₂ vid) ι T₂ = exec (stepCore R₁ vid) ι T₁ := by
  have hDispatchEq : dispatchInstr R₂ vid pi dec = dispatchInstr R₁ vid pi dec := by
    simp only [isCoreOp, Bool.or_eq_true, beq_iff_eq] at hCore
    rcases hCore with (((h | h) | h) | h) | h <;> simp [dispatchInstr, h]
  have hStepEq : exec (stepCore R₂ vid) ι T₁ = exec (stepCore R₁ vid) ι T₁ := by
    simp [stepCore, hpc₁, exec_bind, hdec₁, hDispatchEq]
  have hCongr : exec (stepCore R₂ vid) ι T₂ = exec (stepCore R₂ vid) ι T₁ := by
    apply exec_congr (stepCore R₂ vid) ι T₁ T₂
      (fun k => ∃ op, extOps op = true ∧ isOpLibOrDelegKey op k = true)
    · intro k hk
      apply (hExt k ?_).symm
      intro op hop
      cases his : isOpLibOrDelegKey op k with
      | false => rfl
      | true => exact absurd ⟨op, hop, his⟩ hk
    · rintro k hk ⟨op, hop, his⟩
      rw [hStepEq] at hk
      have hfalse := hUnexposed k hk op hop
      rw [his] at hfalse
      contradiction
  exact hCongr.trans hStepEq

end Pathlang
