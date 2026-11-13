import Pathlang.Syntax

/-!
# Compositional Lemmas for Step Programs

Sequential decomposition (`exec_bind`, `after_bind`), exposure over appended
action lists (`exposed_append`), and key-footprint predicates (`ExecAvoids`)
used across all small-step rules.
-/

namespace Pathlang

namespace Act

variable {K V I E : Type}

/-- Number of external inputs consumed in an action trace. -/
def inputCount : List (Act K V I E) → Nat
  | [] => 0
  | .read _ _ :: as => inputCount as
  | .write _ :: as => inputCount as
  | .input _ :: as => 1 + inputCount as
  | .emit _ :: as => inputCount as

@[simp] theorem inputCount_nil : inputCount ([] : List (Act K V I E)) = 0 := rfl
@[simp] theorem inputCount_read (k : K) (v : Option V) (as : List (Act K V I E)) :
    inputCount (.read k v :: as) = inputCount as := rfl
@[simp] theorem inputCount_write (w : Write K V) (as : List (Act K V I E)) :
    inputCount (.write w :: as) = inputCount as := rfl
@[simp] theorem inputCount_input (i : I) (as : List (Act K V I E)) :
    inputCount (.input i :: as) = 1 + inputCount as := rfl
@[simp] theorem inputCount_emit (e : E) (as : List (Act K V I E)) :
    inputCount (.emit e :: as) = inputCount as := rfl

theorem inputCount_append (as bs : List (Act K V I E)) :
    inputCount (as ++ bs) = inputCount as + inputCount bs := by
  induction as with
  | nil => simp
  | cons a as ih =>
    cases a <;> simp [ih, Nat.add_assoc]

@[simp] theorem events_nil : events ([] : List (Act K V I E)) = [] := rfl
@[simp] theorem events_read (k : K) (v : Option V) (as : List (Act K V I E)) :
    events (.read k v :: as) = events as := rfl
@[simp] theorem events_write (w : Write K V) (as : List (Act K V I E)) :
    events (.write w :: as) = events as := rfl
@[simp] theorem events_input (i : I) (as : List (Act K V I E)) :
    events (.input i :: as) = events as := rfl
@[simp] theorem events_emit (e : E) (as : List (Act K V I E)) :
    events (.emit e :: as) = e :: events as := rfl

theorem events_append (as bs : List (Act K V I E)) :
    events (as ++ bs) = events as ++ events bs := by
  induction as with
  | nil => rfl
  | cons a as ih => cases a <;> simp [ih]

variable [DecidableEq K]

theorem exposed_append (as bs : List (Act K V I E)) (k : K) :
    exposed (as ++ bs) k =
      (exposed as k || (!touches (writes as) k && exposed bs k)) := by
  induction as with
  | nil => simp
  | cons a as ih =>
    cases a with
    | read k' v =>
      simp only [List.cons_append, exposed_read, writes_read, ih]
      cases decide (k' = k) <;> simp
    | write w =>
      simp only [List.cons_append, exposed_write, writes_write, touches_cons, ih]
      cases w.affects k <;> simp [Bool.and_or_distrib_left]
    | input i => simp [ih]
    | emit e => simp [ih]

end Act

section BindLemmas

variable {K V I E α β : Type} [DecidableEq K]

/-- Shifted input stream after executing `P` from `T` under `ι`. -/
def shiftInputs (P : Prog K V I E α) (ι : Nat → I) (T : Store K V) : Nat → I :=
  fun m => ι (Act.inputCount (exec P ι T).2 + m)

theorem exec_bind (P : Prog K V I E α) (f : α → Prog K V I E β)
    (ι : Nat → I) (T : Store K V) :
    exec (Prog.bind P f) ι T =
      let r₁ := exec P ι T
      let r₂ := exec (f r₁.1) (shiftInputs P ι T) (after P ι T)
      (r₂.1, r₁.2 ++ r₂.2) := by
  induction P generalizing ι T with
  | ret a =>
    have hshift : shiftInputs (.ret a : Prog K V I E α) ι T = ι := by
      funext m
      simp [shiftInputs]
    simp [hshift]
  | read k next ih =>
    have hshift : shiftInputs (.read k next) ι T = shiftInputs (next (T k)) ι T := rfl
    simp [exec, ih (T k) ι T, hshift]
  | write w next ih =>
    have hshift : shiftInputs (.write w next) ι T = shiftInputs next ι (w.apply T) := rfl
    simp [exec, ih ι (w.apply T), hshift]
  | input next ih =>
    have hshift : shiftInputs (.input next) ι T =
        shiftInputs (next (ι 0)) (fun n => ι (n + 1)) T := by
      funext m
      simp only [shiftInputs, exec, Act.inputCount_input]
      exact congrArg ι (by omega)
    simp [exec, ih (ι 0) (fun n => ι (n + 1)) T, hshift]
  | emit e next ih =>
    have hshift : shiftInputs (.emit e next) ι T = shiftInputs next ι T := rfl
    simp [exec, ih ι T, hshift]

theorem after_bind (P : Prog K V I E α) (f : α → Prog K V I E β)
    (ι : Nat → I) (T : Store K V) :
    after (Prog.bind P f) ι T =
      after (f (exec P ι T).1) (shiftInputs P ι T) (after P ι T) := by
  conv => lhs; unfold after
  rw [exec_bind]
  simp only [Act.writes_append, applyAll_append]
  rfl

theorem exec_bind_op (P : Prog K V I E α) (f : α → Prog K V I E β)
    (ι : Nat → I) (T : Store K V) :
    exec (P >>= f) ι T =
      let r₁ := exec P ι T
      let r₂ := exec (f r₁.1) (shiftInputs P ι T) (after P ι T)
      (r₂.1, r₁.2 ++ r₂.2) :=
  exec_bind P f ι T

theorem after_bind_op (P : Prog K V I E α) (f : α → Prog K V I E β)
    (ι : Nat → I) (T : Store K V) :
    after (P >>= f) ι T =
      after (f (exec P ι T).1) (shiftInputs P ι T) (after P ι T) :=
  after_bind P f ι T

theorem exec_bind_fst (P : Prog K V I E α) (f : α → Prog K V I E β)
    (ι : Nat → I) (T : Store K V) :
    (exec (P >>= f) ι T).1 =
      (exec (f (exec P ι T).1) (shiftInputs P ι T) (after P ι T)).1 := by
  rw [exec_bind_op]

@[simp] theorem exec_read (k : K) (next : Option V → Prog K V I E α)
    (ι : Nat → I) (T : Store K V) :
    exec (.read k next) ι T =
      ((exec (next (T k)) ι T).1, .read k (T k) :: (exec (next (T k)) ι T).2) := rfl

@[simp] theorem exec_write (w : Write K V) (next : Prog K V I E α)
    (ι : Nat → I) (T : Store K V) :
    exec (.write w next) ι T =
      ((exec next ι (w.apply T)).1, .write w :: (exec next ι (w.apply T)).2) := rfl

@[simp] theorem exec_input (next : I → Prog K V I E α)
    (ι : Nat → I) (T : Store K V) :
    exec (.input next) ι T =
      ((exec (next (ι 0)) (fun n => ι (n + 1)) T).1,
       .input (ι 0) :: (exec (next (ι 0)) (fun n => ι (n + 1)) T).2) := rfl

@[simp] theorem exec_emit (e : E) (next : Prog K V I E α)
    (ι : Nat → I) (T : Store K V) :
    exec (.emit e next) ι T =
      ((exec next ι T).1, .emit e :: (exec next ι T).2) := rfl

/-- Footprint bound on the actions performed by `P` from `(ι, T)`. -/
def ExecAvoids (badRd badWr : K → Prop) (P : Prog K V I E α)
    (ι : Nat → I) (T : Store K V) : Prop :=
  (∀ k v, Act.read k v ∈ (exec P ι T).2 → ¬ badRd k) ∧
  (∀ w, Act.write w ∈ (exec P ι T).2 → ∀ k, w.affects k = true → ¬ badWr k)

theorem ExecAvoids.ret {badRd badWr : K → Prop} (a : α) (ι : Nat → I) (T : Store K V) :
    ExecAvoids badRd badWr (.ret a : Prog K V I E α) ι T :=
  ⟨by simp, by simp⟩

theorem ExecAvoids.rd {badRd badWr : K → Prop} {k : K} (hk : ¬ badRd k)
    (ι : Nat → I) (T : Store K V) :
    ExecAvoids badRd badWr (Prog.rd k : Prog K V I E (Option V)) ι T := by
  refine ⟨?_, by simp⟩
  intro k' v hmem
  simp at hmem
  rcases hmem with ⟨rfl, _⟩
  exact hk

theorem ExecAvoids.wr {badRd badWr : K → Prop} {w : Write K V}
    (hw : ∀ k, w.affects k = true → ¬ badWr k) (ι : Nat → I) (T : Store K V) :
    ExecAvoids badRd badWr (Prog.wr w : Prog K V I E Unit) ι T := by
  refine ⟨by simp, ?_⟩
  intro w' hmem k hk
  simp at hmem
  subst hmem
  exact hw k hk

theorem ExecAvoids.recv {badRd badWr : K → Prop} (ι : Nat → I) (T : Store K V) :
    ExecAvoids badRd badWr (Prog.recv : Prog K V I E I) ι T :=
  ⟨by simp, by simp⟩

theorem ExecAvoids.out {badRd badWr : K → Prop} (e : E) (ι : Nat → I) (T : Store K V) :
    ExecAvoids badRd badWr (Prog.out e : Prog K V I E Unit) ι T :=
  ⟨by simp, by simp⟩

theorem ExecAvoids.bind {badRd badWr : K → Prop}
    {P : Prog K V I E α} {f : α → Prog K V I E β} {ι : Nat → I} {T : Store K V}
    (hP : ExecAvoids badRd badWr P ι T)
    (hf : ExecAvoids badRd badWr (f (exec P ι T).1) (shiftInputs P ι T) (after P ι T)) :
    ExecAvoids badRd badWr (Prog.bind P f) ι T := by
  refine ⟨?_, ?_⟩
  · intro k v hmem
    rw [exec_bind] at hmem
    simp only [List.mem_append] at hmem
    rcases hmem with h1 | h2
    · exact hP.1 k v h1
    · exact hf.1 k v h2
  · intro w hmem k hk
    rw [exec_bind] at hmem
    simp only [List.mem_append] at hmem
    rcases hmem with h1 | h2
    · exact hP.2 w h1 k hk
    · exact hf.2 w h2 k hk

theorem ExecAvoids.bind_op {badRd badWr : K → Prop}
    {P : Prog K V I E α} {f : α → Prog K V I E β} {ι : Nat → I} {T : Store K V}
    (hP : ExecAvoids badRd badWr P ι T)
    (hf : ExecAvoids badRd badWr (f (exec P ι T).1) (shiftInputs P ι T) (after P ι T)) :
    ExecAvoids badRd badWr (P >>= f) ι T :=
  ExecAvoids.bind hP hf

theorem ExecAvoids.read_cont {badRd badWr : K → Prop} {k : K}
    {next : Option V → Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (hk : ¬ badRd k) (hnext : ExecAvoids badRd badWr (next (T k)) ι T) :
    ExecAvoids badRd badWr (.read k next) ι T := by
  refine ⟨?_, ?_⟩
  · intro k' v' hm
    simp only [exec_read, List.mem_cons] at hm
    rcases hm with ⟨rfl, _⟩ | hm
    · exact hk
    · exact hnext.1 k' v' hm
  · intro w hm k' haff
    simp only [exec_read, List.mem_cons] at hm
    rcases hm with h | hm
    · cases h
    · exact hnext.2 w hm k' haff

theorem ExecAvoids.write_cont {badRd badWr : K → Prop} {w : Write K V}
    {next : Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (hw : ∀ k, w.affects k = true → ¬ badWr k)
    (hnext : ExecAvoids badRd badWr next ι (w.apply T)) :
    ExecAvoids badRd badWr (.write w next) ι T := by
  refine ⟨?_, ?_⟩
  · intro k' v' hm
    simp only [exec_write, List.mem_cons] at hm
    rcases hm with h | hm
    · cases h
    · exact hnext.1 k' v' hm
  · intro w' hm k' haff
    simp only [exec_write, List.mem_cons] at hm
    rcases hm with h | hm
    · cases h
      exact hw k' haff
    · exact hnext.2 w' hm k' haff

theorem ExecAvoids.input_cont {badRd badWr : K → Prop}
    {next : I → Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (hnext : ExecAvoids badRd badWr (next (ι 0)) (fun n => ι (n + 1)) T) :
    ExecAvoids badRd badWr (.input next) ι T := by
  refine ⟨?_, ?_⟩
  · intro k' v' hm
    simp only [exec_input, List.mem_cons] at hm
    rcases hm with h | hm
    · cases h
    · exact hnext.1 k' v' hm
  · intro w hm k' haff
    simp only [exec_input, List.mem_cons] at hm
    rcases hm with h | hm
    · cases h
    · exact hnext.2 w hm k' haff

theorem ExecAvoids.emit_cont {badRd badWr : K → Prop} {e : E}
    {next : Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (hnext : ExecAvoids badRd badWr next ι T) :
    ExecAvoids badRd badWr (.emit e next) ι T := by
  refine ⟨?_, ?_⟩
  · intro k' v' hm
    simp only [exec_emit, List.mem_cons] at hm
    rcases hm with h | hm
    · cases h
    · exact hnext.1 k' v' hm
  · intro w hm k' haff
    simp only [exec_emit, List.mem_cons] at hm
    rcases hm with h | hm
    · cases h
    · exact hnext.2 w hm k' haff

theorem ExecAvoids.rd_bind {badRd badWr : K → Prop} {k : K}
    {next : Option V → Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (hk : ¬ badRd k) (hnext : ExecAvoids badRd badWr (next (T k)) ι T) :
    ExecAvoids badRd badWr (Prog.rd k >>= next) ι T := by
  simp only [Prog.bind_eq, Prog.rd_bind]
  exact ExecAvoids.read_cont hk hnext

theorem ExecAvoids.wr_bind {badRd badWr : K → Prop} {w : Write K V}
    {next : PUnit → Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (hw : ∀ k, w.affects k = true → ¬ badWr k)
    (hnext : ExecAvoids badRd badWr (next .unit) ι (w.apply T)) :
    ExecAvoids badRd badWr (Prog.wr w >>= next) ι T := by
  simp only [Prog.bind_eq, Prog.wr_bind]
  exact ExecAvoids.write_cont hw hnext

theorem ExecAvoids.recv_bind {badRd badWr : K → Prop}
    {next : I → Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (hnext : ExecAvoids badRd badWr (next (ι 0)) (fun n => ι (n + 1)) T) :
    ExecAvoids badRd badWr (Prog.recv >>= next) ι T := by
  simp only [Prog.bind_eq, Prog.recv_bind]
  exact ExecAvoids.input_cont hnext

theorem ExecAvoids.out_bind {badRd badWr : K → Prop} {e : E}
    {next : PUnit → Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (hnext : ExecAvoids badRd badWr (next .unit) ι T) :
    ExecAvoids badRd badWr (Prog.out e >>= next) ι T := by
  simp only [Prog.bind_eq, Prog.out_bind]
  exact ExecAvoids.emit_cont hnext

omit [DecidableEq K] in
theorem Act.mem_writes_of_mem {as : List (Act K V I E)} {w : Write K V}
    (hm : Act.write w ∈ as) : w ∈ Act.writes as := by
  induction as with
  | nil => simp at hm
  | cons a as ih =>
    cases a <;> simp only [List.mem_cons] at hm
    · rcases hm with h | hm
      · cases h
      · exact ih hm
    · rcases hm with h | hm
      · cases h
        exact List.mem_cons_self
      · exact List.mem_cons_of_mem _ (ih hm)
    · rcases hm with h | hm
      · cases h
      · exact ih hm
    · rcases hm with h | hm
      · cases h
      · exact ih hm

omit [DecidableEq K] in
theorem Act.mem_of_mem_writes :
    ∀ {as : List (Act K V I E)} {w : Write K V}, w ∈ Act.writes as → Act.write w ∈ as
  | [], _, hw => by simp at hw
  | .read _ _ :: as, _, hw => List.mem_cons_of_mem _ (Act.mem_of_mem_writes hw)
  | .write w' :: as, _, hw => by
      simp only [Act.writes_write, List.mem_cons] at hw
      rcases hw with rfl | hw
      · exact List.mem_cons_self
      · exact List.mem_cons_of_mem _ (Act.mem_of_mem_writes hw)
  | .input _ :: as, _, hw => List.mem_cons_of_mem _ (Act.mem_of_mem_writes hw)
  | .emit _ :: as, _, hw => List.mem_cons_of_mem _ (Act.mem_of_mem_writes hw)

theorem ExecAvoids.not_touches {badRd badWr : K → Prop}
    {P : Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (h : ExecAvoids badRd badWr P ι T) {k : K} (hk : badWr k) :
    touches (Act.writes (exec P ι T).2) k = false := by
  cases ht : touches (Act.writes (exec P ι T).2) k with
  | false => rfl
  | true =>
    obtain ⟨w, hw, haff⟩ := touches_iff.1 ht
    exact absurd hk (h.2 w (Act.mem_of_mem_writes hw) k haff)

theorem ExecAvoids.after_eq {badRd badWr : K → Prop}
    {P : Prog K V I E α} {ι : Nat → I} {T : Store K V}
    (h : ExecAvoids badRd badWr P ι T) {k : K} (hk : badWr k) :
    after P ι T k = T k :=
  applyAll_of_not_touches (h.not_touches hk)

end BindLemmas

end Pathlang
