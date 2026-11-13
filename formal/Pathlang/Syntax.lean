import Pathlang.Crash

/-!
# Paths, Counters, Values, and Frames

Store-level syntax for the v2 operational semantics: thread identifiers,
statuses, `(depth, row)` program counters, partitioned store keys covering
`/vthread`, `/lib`, `/sys`, and user data paths, operand reference headers, and
parametric runtime values.
-/

namespace Pathlang

abbrev Vid := Nat
abbrev SymName := String
abbrev SlotName := String
abbrev Token := String
abbrev ErrMsg := String

/-- Thread execution statuses (`sigma = /vthread/{vid}/..status`). -/
inductive Status where
  | init
  | running
  | wait
  | paused
  | error
  | ok
  deriving DecidableEq, Repr

namespace Status

/-- Statuses that admit a step at the gate (`Run = {init, running, wait}`). -/
def isRun : Status → Bool
  | .init | .running | .wait => true
  | .paused | .error | .ok => false

/-- Non-runnable statuses (`{paused, error, ok}`). -/
def isStopped (s : Status) : Bool := !s.isRun

/-- Statuses from which `resume` is enabled (`{paused, error}`). -/
def isResumable : Status → Bool
  | .paused | .error => true
  | .init | .running | .wait | .ok => false

@[simp] theorem isRun_init : isRun .init = true := rfl
@[simp] theorem isRun_running : isRun .running = true := rfl
@[simp] theorem isRun_wait : isRun .wait = true := rfl
@[simp] theorem isRun_paused : isRun .paused = false := rfl
@[simp] theorem isRun_error : isRun .error = false := rfl
@[simp] theorem isRun_ok : isRun .ok = false := rfl

@[simp] theorem isResumable_paused : isResumable .paused = true := rfl
@[simp] theorem isResumable_error : isResumable .error = true := rfl
@[simp] theorem isResumable_running : isResumable .running = false := rfl
@[simp] theorem isResumable_init : isResumable .init = false := rfl
@[simp] theorem isResumable_wait : isResumable .wait = false := rfl
@[simp] theorem isResumable_ok : isResumable .ok = false := rfl

end Status

/-- Program counter `(depth, row)` with 1-based frame depth and instruction row. -/
structure PC where
  depth : Nat
  row : Nat
  deriving DecidableEq, Repr

namespace PC

def valid (pi : PC) : Bool :=
  decide (1 ≤ pi.depth ∧ 1 ≤ pi.row)

def next (pi : PC) : PC :=
  ⟨pi.depth, pi.row + 1⟩

def target (pi : PC) (n : Nat) : PC :=
  ⟨pi.depth, n⟩

def entry (pi : PC) : PC :=
  ⟨pi.depth + 1, 1⟩

@[simp] theorem next_depth (pi : PC) : pi.next.depth = pi.depth := rfl
@[simp] theorem next_row (pi : PC) : pi.next.row = pi.row + 1 := rfl
@[simp] theorem target_depth (pi : PC) (n : Nat) : (pi.target n).depth = pi.depth := rfl
@[simp] theorem target_row (pi : PC) (n : Nat) : (pi.target n).row = n := rfl
@[simp] theorem entry_depth (pi : PC) : pi.entry.depth = pi.depth + 1 := rfl
@[simp] theorem entry_row (pi : PC) : pi.entry.row = 1 := rfl

theorem valid_iff (pi : PC) : pi.valid = true ↔ 1 ≤ pi.depth ∧ 1 ≤ pi.row := by
  simp [valid]

theorem next_valid {pi : PC} (h : pi.valid = true) : pi.next.valid = true := by
  simp only [valid_iff, next_depth, next_row] at h ⊢
  exact ⟨h.1, by omega⟩

theorem entry_valid (pi : PC) : pi.entry.valid = true := by
  simp only [valid_iff, entry_depth, entry_row]
  exact ⟨by omega, by omega⟩

theorem target_valid {pi : PC} {n : Nat} (h : pi.valid = true) (hn : 1 ≤ n) :
    (pi.target n).valid = true := by
  simp only [valid_iff, target_depth, target_row] at h ⊢
  exact ⟨h.1, hn⟩

end PC

/-- Parameter and operand axis: negative columns (`rd`) vs positive columns (`wr`). -/
inductive ParamDir where
  | rd
  | wr
  deriving DecidableEq, Repr

/-- Store keys across `/vthread`, `/lib`, `/sys`, and user data paths. -/
inductive Key where
  /-- `/vthread/{vid}/..pc` (`kappa`) -/
  | pcKey (vid : Vid)
  /-- `/vthread/{vid}/..status` (`sigma`) -/
  | statusKey (vid : Vid)
  /-- `/vthread/{vid}/..stop` (`rho`) -/
  | stopKey (vid : Vid)
  /-- `/vthread/{vid}/..driver` (`lambda`) -/
  | driverKey (vid : Vid)
  /-- `/vthread/{vid}/..msg` (`mu`) -/
  | msgKey (vid : Vid)
  /-- `/vthread/{vid}/[d]/..lib` -/
  | frameLib (vid : Vid) (d : Nat)
  /-- `/vthread/{vid}/[d]..callpc` -/
  | frameCallPc (vid : Vid) (d : Nat)
  /-- `/vthread/{vid}/[d]..returnpc` -/
  | frameRetPc (vid : Vid) (d : Nat)
  /-- `/vthread/{vid}/[d]/` -/
  | frameDir (vid : Vid) (d : Nat)
  /-- `/vthread/{vid}/[d]/[0, +-j]` -/
  | frameParam (vid : Vid) (d : Nat) (dir : ParamDir) (j : Nat)
  /-- `/vthread/{vid}/[d]/{slot}` -/
  | frameSlot (vid : Vid) (d : Nat) (slot : SlotName)
  /-- `/lib/{name}` (`rwfunc` marker or extension route header) -/
  | libHeader (name : SymName)
  /-- `/lib/{name}/[0,0]` (signature `(nr, nw)` header) -/
  | libSig (name : SymName)
  /-- `/lib/{name}.[0, +-j]` (parameter definition cell) -/
  | libParamDef (name : SymName) (dir : ParamDir) (j : Nat)
  /-- `/lib/{name}/[s0, 0]` (instruction opcode and `(nr, nw)` count header) -/
  | libInstr (name : SymName) (row : Nat)
  /-- `/lib/{name}/[s0, +-j]` (instruction operand cell) -/
  | libOperand (name : SymName) (row : Nat) (dir : ParamDir) (j : Nat)
  /-- `/lib/{op}/vids/{vid}` (extension queue entry) -/
  | queueEntry (op : SymName) (vid : Vid)
  /-- `/sys/rwir-backend/{op}` (delegation entry) -/
  | delegEntry (op : SymName)
  /-- User or world data path outside `/vthread`, `/lib`, and `/sys` -/
  | dataKey (segs : List String)
  deriving DecidableEq, Repr

/-- Base slot named by an operand reference before following pointer hops. -/
inductive BaseRef where
  | localSlot (slot : SlotName)
  | paramSlot (dir : ParamDir) (j : Nat)
  | absKey (k : Key)
  deriving DecidableEq, Repr

namespace BaseRef

/-- Resolve the starting key of `b` in thread `vid` at frame depth `d`. -/
def startKey (vid : Vid) (d : Nat) : BaseRef → Key
  | .localSlot s => .frameSlot vid d s
  | .paramSlot dir j => .frameParam vid d dir j
  | .absKey k => k

end BaseRef

/-- Store values, parametric in opaque user/handler payload type `D`. -/
inductive Val (D : Type) where
  | status (s : Status)
  | stopMark
  | lease (tok : Token)
  | errMsg (msg : ErrMsg)
  | pc (pi : PC)
  | sym (name : SymName)
  | sig (nr nw : Nat)
  | instr (op : SymName) (nr nw : Nat)
  | rwfuncHdr
  | routeHdr (runtime : String)
  | delegTarget (runtime : String)
  | ref (hops : Nat) (base : BaseRef)
  | ptr (target : Key)
  | int (n : Int)
  | bool (b : Bool)
  | data (payload : D)
  deriving DecidableEq, Repr

/-- Membership in frame `d` of `vid`, including its directory, slots, and owned
system keys (`..lib`, `..callpc`, `..returnpc`). -/
def inFrame (vid : Vid) (d : Nat) : Key → Bool
  | .frameLib v d' => decide (v = vid ∧ d' = d)
  | .frameCallPc v d' => decide (v = vid ∧ d' = d)
  | .frameRetPc v d' => decide (v = vid ∧ d' = d)
  | .frameDir v d' => decide (v = vid ∧ d' = d)
  | .frameParam v d' _ _ => decide (v = vid ∧ d' = d)
  | .frameSlot v d' _ => decide (v = vid ∧ d' = d)
  | _ => false

/-- Keys inside any frame `e` of `vid` that is dead at `liveDepth` (`e > liveDepth` or `e = 0`). -/
def isDeadFrameKey (vid : Vid) (liveDepth : Nat) : Key → Bool
  | .frameLib v e => decide (v = vid ∧ (e = 0 ∨ liveDepth < e))
  | .frameCallPc v e => decide (v = vid ∧ (e = 0 ∨ liveDepth < e))
  | .frameRetPc v e => decide (v = vid ∧ (e = 0 ∨ liveDepth < e))
  | .frameDir v e => decide (v = vid ∧ (e = 0 ∨ liveDepth < e))
  | .frameParam v e _ _ => decide (v = vid ∧ (e = 0 ∨ liveDepth < e))
  | .frameSlot v e _ => decide (v = vid ∧ (e = 0 ∨ liveDepth < e))
  | _ => false

/-- Thread control keys (`kappa`, `sigma`, `rho`, `lambda`, `mu`), frame system
keys (`..lib`, `..callpc`, `..returnpc`), and queue entries of `vid`. -/
def isThreadControlKey (vid : Vid) : Key → Bool
  | .pcKey v => decide (v = vid)
  | .statusKey v => decide (v = vid)
  | .stopKey v => decide (v = vid)
  | .driverKey v => decide (v = vid)
  | .msgKey v => decide (v = vid)
  | .frameLib v _ => decide (v = vid)
  | .frameCallPc v _ => decide (v = vid)
  | .frameRetPc v _ => decide (v = vid)
  | .queueEntry _ v => decide (v = vid)
  | _ => false

/-- Parameter binding slots `[e]/[0, +-j]` of `vid`. -/
def isFrameParamKey (vid : Vid) : Key → Bool
  | .frameParam v _ _ _ => decide (v = vid)
  | _ => false

/-- Keys under `/lib/{name}` or `/sys/rwir-backend/{name}` for a given operation name. -/
def isOpLibOrDelegKey (op : SymName) : Key → Bool
  | .libHeader n => decide (n = op)
  | .libSig n => decide (n = op)
  | .libParamDef n _ _ => decide (n = op)
  | .libInstr n _ => decide (n = op)
  | .libOperand n _ _ _ => decide (n = op)
  | .queueEntry n _ => decide (n = op)
  | .delegEntry n => decide (n = op)
  | _ => false

theorem inFrame_stopKey (vid v : Vid) (d : Nat) : inFrame vid d (.stopKey v) = false := rfl
theorem inFrame_driverKey (vid v : Vid) (d : Nat) : inFrame vid d (.driverKey v) = false := rfl
theorem inFrame_statusKey (vid v : Vid) (d : Nat) : inFrame vid d (.statusKey v) = false := rfl
theorem inFrame_pcKey (vid v : Vid) (d : Nat) : inFrame vid d (.pcKey v) = false := rfl
theorem inFrame_msgKey (vid v : Vid) (d : Nat) : inFrame vid d (.msgKey v) = false := rfl

theorem inFrame_iff_dead_of_gt {vid : Vid} {d e : Nat} {k : Key}
    (he : d < e) (hk : inFrame vid e k = true) :
    isDeadFrameKey vid d k = true := by
  cases k <;> simp only [inFrame, isDeadFrameKey, decide_eq_true_eq] at hk ⊢ <;>
    rcases hk with ⟨rfl, rfl⟩ <;> exact ⟨rfl, Or.inr he⟩

theorem not_inFrame_of_ne {vid : Vid} {d₁ d₂ : Nat} {k : Key}
    (hne : d₁ ≠ d₂) (hk : inFrame vid d₁ k = true) :
    inFrame vid d₂ k = false := by
  cases k <;> simp only [inFrame, decide_eq_true_eq, decide_eq_false_iff_not] at hk ⊢ <;>
    rcases hk with ⟨rfl, rfl⟩ <;> intro ⟨_, heq⟩ <;> exact hne heq

end Pathlang
