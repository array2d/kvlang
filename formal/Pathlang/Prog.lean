import Pathlang.Store

/-!
# Step programs

A step reads and writes single store keys, may receive external inputs
(clocks, random sources), and may emit observable events. `Prog` is the free
monad over these operations, and `exec` runs a program from a store, returning
its result and the actions it performed, in order.

`exec_congr` is the read-dependence half of operand locality (paper
Lemma 10, `lem:locality`): if two stores agree on every key that a run reads before writing
it, the runs are identical. It holds for every `Prog`, so every rule written as
a `Prog` gets it without a separate proof; what remains for each rule is to
compute which keys its runs read.
-/

namespace Pathlang

/-- A step program over keys `K`, values `V`, external inputs `I`, and events
`E`, with result type `α`. -/
inductive Prog (K V I E α : Type) where
  | ret (a : α)
  | read (k : K) (next : Option V → Prog K V I E α)
  | write (w : Write K V) (next : Prog K V I E α)
  | input (next : I → Prog K V I E α)
  | emit (e : E) (next : Prog K V I E α)

namespace Prog

variable {K V I E α β : Type}

/-- Sequential composition. -/
def bind : Prog K V I E α → (α → Prog K V I E β) → Prog K V I E β
  | ret a, f => f a
  | read k next, f => read k fun v => bind (next v) f
  | write w next, f => write w (bind next f)
  | input next, f => input fun i => bind (next i) f
  | emit e next, f => emit e (bind next f)

instance : Monad (Prog K V I E) where
  pure := ret
  bind := bind

@[simp] theorem pure_eq (a : α) : (pure a : Prog K V I E α) = ret a := rfl

@[simp] theorem bind_eq (x : Prog K V I E α) (f : α → Prog K V I E β) :
    x >>= f = bind x f := rfl

@[simp] theorem ret_bind (a : α) (f : α → Prog K V I E β) : bind (ret a) f = f a := rfl

@[simp] theorem read_bind (k : K) (next : Option V → Prog K V I E α)
    (f : α → Prog K V I E β) : bind (read k next) f = read k fun v => bind (next v) f := rfl

@[simp] theorem write_bind (w : Write K V) (next : Prog K V I E α)
    (f : α → Prog K V I E β) : bind (write w next) f = write w (bind next f) := rfl

@[simp] theorem input_bind (next : I → Prog K V I E α) (f : α → Prog K V I E β) :
    bind (input next) f = input fun i => bind (next i) f := rfl

@[simp] theorem emit_bind (e : E) (next : Prog K V I E α) (f : α → Prog K V I E β) :
    bind (emit e next) f = emit e (bind next f) := rfl

/-- Read the value at `k`. -/
def rd (k : K) : Prog K V I E (Option V) := read k ret

/-- Perform one store operation. -/
def wr (w : Write K V) : Prog K V I E Unit := write w (ret ())

/-- Receive the next external input. -/
def recv : Prog K V I E I := input ret

/-- Emit an observable event. -/
def out (e : E) : Prog K V I E Unit := emit e (ret ())

@[simp] theorem rd_bind (k : K) (f : Option V → Prog K V I E β) :
    bind (rd k) f = read k f := rfl

@[simp] theorem wr_bind (w : Write K V) (f : Unit → Prog K V I E β) :
    bind (wr w) f = write w (f ()) := rfl

@[simp] theorem recv_bind (f : I → Prog K V I E β) : bind recv f = input f := rfl

@[simp] theorem out_bind (e : E) (f : Unit → Prog K V I E β) :
    bind (out e) f = emit e (f ()) := rfl

end Prog

/-- One action of a run. A read records the value it saw. -/
inductive Act (K V I E : Type) where
  | read (k : K) (v : Option V)
  | write (w : Write K V)
  | input (i : I)
  | emit (e : E)

namespace Act

variable {K V I E : Type}

/-- The store operations among the actions, in order. -/
def writes : List (Act K V I E) → List (Write K V)
  | [] => []
  | read _ _ :: as => writes as
  | write w :: as => w :: writes as
  | input _ :: as => writes as
  | emit _ :: as => writes as

/-- The events among the actions, in order. -/
def events : List (Act K V I E) → List E
  | [] => []
  | read _ _ :: as => events as
  | write _ :: as => events as
  | input _ :: as => events as
  | emit e :: as => e :: events as

@[simp] theorem writes_nil : writes ([] : List (Act K V I E)) = [] := rfl
@[simp] theorem writes_read (k : K) (v : Option V) (as : List (Act K V I E)) :
    writes (read k v :: as) = writes as := rfl
@[simp] theorem writes_write (w : Write K V) (as : List (Act K V I E)) :
    writes (write w :: as) = w :: writes as := rfl
@[simp] theorem writes_input (i : I) (as : List (Act K V I E)) :
    writes (input i :: as) = writes as := rfl
@[simp] theorem writes_emit (e : E) (as : List (Act K V I E)) :
    writes (emit e :: as) = writes as := rfl

theorem writes_append (as bs : List (Act K V I E)) :
    writes (as ++ bs) = writes as ++ writes bs := by
  induction as with
  | nil => rfl
  | cons a as ih => cases a <;> simp [ih]

variable [DecidableEq K]

/-- `exposed as k` holds iff the actions read `k` before any of them writes it. -/
def exposed : List (Act K V I E) → K → Bool
  | [], _ => false
  | read k' _ :: as, k => decide (k' = k) || exposed as k
  | write w :: as, k => !w.affects k && exposed as k
  | input _ :: as, k => exposed as k
  | emit _ :: as, k => exposed as k

@[simp] theorem exposed_nil (k : K) : exposed ([] : List (Act K V I E)) k = false := rfl
@[simp] theorem exposed_read (k' : K) (v : Option V) (as : List (Act K V I E)) (k : K) :
    exposed (read k' v :: as) k = (decide (k' = k) || exposed as k) := rfl
@[simp] theorem exposed_write (w : Write K V) (as : List (Act K V I E)) (k : K) :
    exposed (write w :: as) k = (!w.affects k && exposed as k) := rfl
@[simp] theorem exposed_input (i : I) (as : List (Act K V I E)) (k : K) :
    exposed (input i :: as) k = exposed as k := rfl
@[simp] theorem exposed_emit (e : E) (as : List (Act K V I E)) (k : K) :
    exposed (emit e :: as) k = exposed as k := rfl

end Act

variable {K V I E α : Type} [DecidableEq K]

/-- Run `P` from store `T`, where `ι n` is the `n`-th external input. Returns
the result and the actions performed, in order. A read sees the store as
updated by the program's earlier writes. -/
def exec : Prog K V I E α → (Nat → I) → Store K V → α × List (Act K V I E)
  | .ret a, _, _ => (a, [])
  | .read k next, ι, T =>
      let r := exec (next (T k)) ι T
      (r.1, .read k (T k) :: r.2)
  | .write w next, ι, T =>
      let r := exec next ι (w.apply T)
      (r.1, .write w :: r.2)
  | .input next, ι, T =>
      let r := exec (next (ι 0)) (fun n => ι (n + 1)) T
      (r.1, .input (ι 0) :: r.2)
  | .emit e next, ι, T =>
      let r := exec next ι T
      (r.1, .emit e :: r.2)

@[simp] theorem exec_ret (a : α) (ι : Nat → I) (T : Store K V) :
    exec (Prog.ret a : Prog K V I E α) ι T = (a, []) := rfl

@[simp] theorem exec_rd (k : K) (ι : Nat → I) (T : Store K V) :
    exec (Prog.rd k : Prog K V I E (Option V)) ι T = (T k, [.read k (T k)]) := rfl

@[simp] theorem exec_wr (w : Write K V) (ι : Nat → I) (T : Store K V) :
    exec (Prog.wr w : Prog K V I E Unit) ι T = ((), [.write w]) := rfl

@[simp] theorem exec_recv (ι : Nat → I) (T : Store K V) :
    exec (Prog.recv : Prog K V I E I) ι T = (ι 0, [.input (ι 0)]) := rfl

@[simp] theorem exec_out (e : E) (ι : Nat → I) (T : Store K V) :
    exec (Prog.out e : Prog K V I E Unit) ι T = ((), [.emit e]) := rfl

/-- The store a run of `P` from `T` ends in. -/
def after (P : Prog K V I E α) (ι : Nat → I) (T : Store K V) : Store K V :=
  applyAll T (Act.writes (exec P ι T).2)

/-! `after` is the store threaded through the run: -/

@[simp] theorem after_ret (a : α) (ι : Nat → I) (T : Store K V) :
    after (.ret a : Prog K V I E α) ι T = T := rfl

@[simp] theorem after_read (k : K) (next : Option V → Prog K V I E α) (ι : Nat → I)
    (T : Store K V) : after (.read k next) ι T = after (next (T k)) ι T := rfl

@[simp] theorem after_write (w : Write K V) (next : Prog K V I E α) (ι : Nat → I)
    (T : Store K V) : after (.write w next) ι T = after next ι (w.apply T) := rfl

@[simp] theorem after_input (next : I → Prog K V I E α) (ι : Nat → I) (T : Store K V) :
    after (.input next) ι T = after (next (ι 0)) (fun n => ι (n + 1)) T := rfl

@[simp] theorem after_emit (e : E) (next : Prog K V I E α) (ι : Nat → I) (T : Store K V) :
    after (.emit e next) ι T = after next ι T := rfl

/-- **Read dependence.** If `R` agrees with `S` outside `D`, and the run of `P`
from `S` reads no key of `D` before writing it, then the run from `R` has the
same result and the same actions, including every value read. -/
theorem exec_congr (P : Prog K V I E α) (ι : Nat → I) (S R : Store K V) (D : K → Prop)
    (hD : ∀ k, ¬ D k → R k = S k)
    (hx : ∀ k, Act.exposed (exec P ι S).2 k = true → ¬ D k) :
    exec P ι R = exec P ι S := by
  induction P generalizing ι S R D with
  | ret a => rfl
  | read k next ih =>
    have hk : R k = S k := hD k (hx k (by simp [exec]))
    have hrest : exec (next (S k)) ι R = exec (next (S k)) ι S :=
      ih (S k) ι S R D hD fun k' hk' => hx k' (by simp [exec, hk'])
    simp only [exec, hk, hrest]
  | write w next ih =>
    have hrest : exec next ι (w.apply R) = exec next ι (w.apply S) := by
      apply ih ι (w.apply S) (w.apply R) (fun k => D k ∧ w.affects k = false)
      · intro k hk
        cases hw : w.affects k with
        | true => rw [Write.apply_of_affects R hw, Write.apply_of_affects S hw]
        | false =>
          rw [Write.apply_of_not_affects R hw, Write.apply_of_not_affects S hw]
          exact hD k fun hDk => hk ⟨hDk, hw⟩
      · rintro k hk ⟨hDk, hw⟩
        exact hx k (by simp [exec, hw, hk]) hDk
    simp only [exec, hrest]
  | input next ih =>
    have hrest : exec (next (ι 0)) (fun n => ι (n + 1)) R =
        exec (next (ι 0)) (fun n => ι (n + 1)) S :=
      ih (ι 0) (fun n => ι (n + 1)) S R D hD fun k hk => hx k (by simp [exec, hk])
    simp only [exec, hrest]
  | emit e next ih =>
    have hrest : exec next ι R = exec next ι S :=
      ih ι S R D hD fun k hk => hx k (by simp [exec, hk])
    simp only [exec, hrest]

/-- If moreover `R` and `S` differ only at keys the run writes, the runs end in
the same store. -/
theorem after_congr (P : Prog K V I E α) (ι : Nat → I) (S R : Store K V) (D : K → Prop)
    (hD : ∀ k, ¬ D k → R k = S k)
    (hx : ∀ k, Act.exposed (exec P ι S).2 k = true → ¬ D k)
    (hw : ∀ k, D k → touches (Act.writes (exec P ι S).2) k = true) :
    after P ι R = after P ι S := by
  unfold after
  rw [exec_congr P ι S R D hD hx]
  apply applyAll_congr
  intro k hk
  apply hD
  intro hDk
  simp [hw k hDk] at hk

end Pathlang
