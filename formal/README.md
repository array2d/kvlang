# Mechanization

A Lean 4 mechanization of the pathlang store-level semantics, cooperative
stop/resume reduction, live-edit control properties, and scoped crash recovery.
It depends only on Lean 4's core library (`lean-toolchain` pins Lean 4.35.0, no
Mathlib) and contains zero `sorry` and zero custom axioms.

## Scope

The development covers the store-level operational model of Sections 2-6 and
Appendix A:

- **In scope.** Flat path stores and non-atomic subtree deletes (`delWhere`);
  step programs as a free monad over single-key reads, writes, external inputs,
  and events; the v2 stop gate (`rd(rho)` absent and `sigma in Run`), pause
  transition (`put(sigma, paused)`), and all small-step rules of Figure 3
  (`Copy`, `Goto`, `Br`, `Call`, `Return`, `Native`, `Deleg`, `Handoff`, plus
  `UserCall` syntactic sugar); all labeled theorems, lemmas, invariants, and
  propositions in the paper (`thm:reduction`, `inv:commit`, `thm:resumption`, `thm:crash`,
  `thm:edit`, `prop:failstop`, `prop:core-invariance`, `lem:approx`,
  `lem:determinism`, `lem:locality`, `lem:footprint`, `lem:unread`); live-edit
  statement splicing (C1) and row relocation (C2) simulations; and the scoped
  crash-recovery theorem with concrete crash dichotomies and ordering
  counterexamples for `Goto`, `Br`, `Copy`, `Call`, `UserCall`, and `Return`.
- **Parameters, as in the paper.** Payload domain `D`, external input type `I`,
  observable event type `E`, native capability handlers satisfying `(H1)`, and
  delegation/handoff extensions satisfying `(H2)`. Values are modeled as an
  algebraic sum (`Val D`) covering pointers, integers, booleans, program
  counters, symbol names, statuses, library/delegation headers, and abstract
  payloads; wire encoding is not modeled.
- **Out of scope: runtime implementations.** Optimizing or compiled runtimes
  that buffer state outside the store or commit the program counter less than
  once per step are connected to the semantics via conformance and fault
  injection rather than a compiler verification proof.

## Build

Install [elan](https://github.com/leanprover/elan). `lean-toolchain` pins
Lean 4.35.0, and elan fetches it on first use.

```sh
lake build --wfail         # check every proof with warnings treated as errors
lake env lean Audit.lean   # print axioms for all 76 main declarations
```

Every audited declaration depends only on Lean's standard axioms (`propext`,
`Classical.choice`, `Quot.sound`) or on no axioms at all.

## Checks

`scripts/check.sh` runs four independent checks:

| Check | What it rules out |
|---|---|
| `lake build --wfail` | an incomplete proof or any compiler warning, including `sorry` |
| [axiom-audit](https://github.com/leanprover-community/axiom-audit) | any declaration under `Pathlang` depending on an axiom outside `propext`, `Classical.choice`, and `Quot.sound` (including `sorryAx` and `native_decide`) |
| `leanchecker` | environment corruption or declarations bypassing Lean's kernel |
| [nanoda](https://github.com/ammkrn/nanoda_lib) | a soundness bug in Lean's kernel, checked by an independent kernel written in Rust |

## Files

| File | Lines | Content |
|---|---:|---|
| `Pathlang/Store.lean` | 148 | Flat stores; atomic `put` and `del`, plus non-atomic `delWhere` for subtree deletes; `applyAll` and last-writer-wins lemmas (`applyAll_congr`, `applyAll_absorb`, `applyAll_take_absorb`). |
| `Pathlang/Prog.lean` | 261 | Step programs (`Prog`) as a free monad over single-key reads, writes, external inputs, and events; `exec`, `after`, `exposed`, `exec_congr`, and `after_congr`. |
| `Pathlang/Crash.lean` | 202 | Action-level crash cuts (`CrashAt`), including partial `delWhere` interruption; write-after-read freedom at a cut (`WarFreeAt`); `replay` from any WAR-free cut. |
| `Pathlang/Syntax.lean` | 260 | Structured store paths (`Key`), statuses (`Status`), 1-indexed program counters (`PC`), typed values (`Val D`), frame predicates (`inFrame`, `isDeadFrameKey`), and control-key classifiers. |
| `Pathlang/ProgLemmas.lean` | 374 | Monadic bind decomposition (`exec_bind`, `after_bind`, `shiftInputs`) and compositional read/write avoidance (`ExecAvoids`). |
| `Pathlang/Rules.lean` | 782 | Figure 3 small-step rules as `Prog` programs (`copyRuleProg`, `gotoRuleProg`, `brRuleProg`, `callRuleProg`, `returnRuleProg`, `nativeRuleProg`, `delegRuleProg`, `handoffRuleProg`, `userCallRuleProg`); `(H1)`/`(H2)` (`HandlerSpec`, `ExtensionSpec`, `RuntimeSpec`); `gatedStep` and `GatedRel`; determinism and core invariance. |
| `Pathlang/Footprint.lean` | 1,605 | Pure decoders (`decodeInstrPure`, `resolveReadPure`, `resolveWritePure`), program discipline `(P)` (`StepDiscipline`), gate/status footprint (`gatedStep_footprint`), Lipton right-mover reduction (`theorem1_v2_reduction`), and next-gate pause corollaries. |
| `Pathlang/Agreement.lean` | 2,883 | Store agreement `~` (`StoreAgree`), lease `createIfAbsent` mutual exclusion (`lease_mutual_exclusion`), preservation of `(P)` and `~` across steps (`StepDiscipline.of_storeAgree`, `step_respects_agree`), and Theorem 3 (`stop_resume_resumption`, `stop_resume_isPrefix`, `stop_resume_completed`). |
| `Pathlang/Control.lean` | 1,804 | Unread-paths lemmas (`gatedStep_unread`, `gatedStep_unread_strict`, `uninterrupted_unread`), Theorem 5 (a) and (b) (`stop_edit_resume_det`, `stop_edit_resume_unread_prefix`, `stop_edit_resume_unread`), C1 spliced-program live-edit equivalence (`live_edit_spliced_program_equiv`), and C2 row-relocation simulations and counterexample. |
| `Pathlang/Invariants.lean` | 2,151 | `vthreadCreate` initialization, Commit-Point Completeness (`CommitComplete`, Invariant 2) across gated steps and resumable runs, `libSig`-overwrite counterexample, and Fail-Stop (Proposition 6). |
| `Pathlang/StepCrash.lean` | 2,331 | Scoped crash-recovery theorem (`StepWarFree`, `scoped_crash_recovery`, `scoped_crash_recovery_write_prefix`), pre-commit vs. post-commit crash dichotomy (`commitSplit_crash_dichotomy`), and concrete crash recovery theorems and counterexamples for `Goto`, `Br`, `Copy`, `Call`, `UserCall`, and `Return`. |
| `Pathlang/Locality.lean` | 1,150 | Appendix A.3 operand-locality lemma (`gatedStep_locality_no_ext`, `gatedStep_locality`) bounding every read and write of `gatedStep`. |
| `Pathlang/Examples/AliasedIncrement.lean` | 159 | Standalone toy example: `i + 1 -> i` fails WAR-freedom (`incr_not_A0`), while staging through a temporary restores WAR-freedom (`lowered_A0`). |
| `Pathlang/Examples/AliasedCall.lean` | 133 | Standalone toy example: aliased call `inc(x) -> (x)` fails WAR-freedom (`aliased_not_A0`), while distinct arguments satisfy WAR-freedom (`distinct_A0`). |
| `Pathlang.lean` | 14 | Root module importing all `Pathlang` modules. |
| `Audit.lean` | 85 | `#print axioms` checks for 76 main theorems across the development. |
| **Total** | **14,342** | **16 Lean source files, 0 `sorry`, 0 custom axioms.** |

## Correspondence with the Paper

| Paper Item | Lean Declaration(s) | Location |
|---|---|---|
| Section 2: Flat store, atomic single-path writes, non-atomic `deltree` | `Store`, `Write`, `Write.apply`, `applyAll`, `CrashAt` | `Pathlang/Store.lean:18`, `Pathlang/Store.lean:21`, `Pathlang/Crash.lean:52` |
| Section 3 (Fig. 3): Small-step rules `Copy`, `Goto`, `Br`, `Call`, `Return`, `Native`, `Deleg`, `Handoff`, `UserCall` | `copyRuleProg`, `gotoRuleProg`, `brRuleProg`, `callRuleProg`, `returnRuleProg`, `nativeRuleProg`, `delegRuleProg`, `handoffRuleProg`, `userCallRuleProg` | `Pathlang/Rules.lean:337-507` |
| Section 3 (Fig. 3): Stop gate (`rd(rho)` absent, `sigma in Run`) and pause transition (`put(sigma, paused)`) | `pauseAfterGate`, `gatedStep`, `StoreRunnable`, `GatedRel` | `Pathlang/Rules.lean:569`, `Pathlang/Rules.lean:576`, `Pathlang/Rules.lean:632`, `Pathlang/Rules.lean:638` |
| Appendix A: Hypotheses `(H1)` (native handler footprint/determinism) and `(H2)` (extension isolation) | `HandlerSpec`, `ExtensionSpec`, `RuntimeSpec` | `Pathlang/Rules.lean:56`, `Pathlang/Rules.lean:85`, `Pathlang/Rules.lean:115` |
| Appendix A: Program discipline `(P)` on operand resolution and targets | `ReadOpSafe`, `WriteOpSafe`, `StepDiscipline` | `Pathlang/Footprint.lean:241`, `Pathlang/Footprint.lean:246`, `Pathlang/Footprint.lean:256` |
| Lemma 9 (`lem:determinism`): Guard exclusivity and single-step determinism | `ruleGuard_unique`, `gatedRel_iff_exec`, `gatedRel_deterministic` | `Pathlang/Rules.lean:626`, `Pathlang/Rules.lean:667`, `Pathlang/Rules.lean:726` |
| Lemma 10 (`lem:locality`): Operand locality and read dependence | `exec_congr`, `after_congr`, `gatedStep_locality_no_ext`, `gatedStep_locality` | `Pathlang/Prog.lean:213`, `Pathlang/Prog.lean:248`, `Pathlang/Locality.lean:944`, `Pathlang/Locality.lean:1047` |
| Proposition 7 (`prop:core-invariance`): Core invariance under extension registration | `core_invariance` | `Pathlang/Rules.lean:751` |
| Lemma 11 (`lem:footprint`): Gate/status footprint (`rho` read only at gate; `rho`, `lambda` never written) | `gatedStep_footprint` | `Pathlang/Footprint.lean:1164` |
| Theorem 1 (`thm:reduction`): In-step `stop = put(rho, 1)` right-mover reduction to step boundary | `gatedContinuation_right_mover`, `theorem1_v2_reduction` | `Pathlang/Footprint.lean:1383`, `Pathlang/Footprint.lean:1493` |
| Theorem 1 corollaries: Stop takes effect at the very next gate | `stop_takes_effect_at_next_gate`, `midstep_stop_pauses_next_gate` | `Pathlang/Footprint.lean:1564`, `Pathlang/Footprint.lean:1589` |
| Section 4: Store agreement `~` (ignoring `sigma`, `mu`, `rho`, `lambda`, and dead frames `d' > d`) | `IgnoredByAgree`, `StoreAgree`, `StoreAgree.refl`, `StoreAgree.symm`, `StoreAgree.trans` | `Pathlang/Agreement.lean:36`, `Pathlang/Agreement.lean:43`, `Pathlang/Agreement.lean:61-72` |
| Section 4: Lease `create-if-absent` mutual exclusion (`lambda`) | `createIfAbsent`, `lease_mutual_exclusion` | `Pathlang/Agreement.lean:185`, `Pathlang/Agreement.lean:225` |
| Lemma 8 (`lem:approx`): Steps preserve `(P)` and respect `~` | `StepDiscipline.of_storeAgree`, `step_respects_agree` | `Pathlang/Agreement.lean:331`, `Pathlang/Agreement.lean:2209` |
| Theorem 3 (`thm:resumption`): Stop and resume trace-prefix and `~`-equivalence (and final agreement) | `stop_resume_resumption`, `stop_resume_isPrefix`, `stop_resume_completed` | `Pathlang/Agreement.lean:2554`, `Pathlang/Agreement.lean:2828`, `Pathlang/Agreement.lean:2856` |
| Lemma 12 (`lem:unread`): Unread paths do not affect step or multi-step execution | `gatedStep_unread`, `gatedStep_unread_strict`, `uninterrupted_unread` | `Pathlang/Control.lean:113`, `Pathlang/Control.lean:226`, `Pathlang/Control.lean:347` |
| Theorem 5(a) (`thm:edit`): Deterministic continuation from edited stopped store | `stop_edit_resume_det` | `Pathlang/Control.lean:717` |
| Theorem 5(b) (`thm:edit`): Observational invisibility of edits to subsequently unread paths | `stop_edit_resume_unread_prefix`, `stop_edit_resume_unread` | `Pathlang/Control.lean:1168`, `Pathlang/Control.lean:1241` |
| Section 5 (`edit-tool` C1): Compositional spliced-program live-edit equivalence | `live_edit_spliced_program_equiv` | `Pathlang/Control.lean:1368` |
| Section 5 (`edit-tool` C2): Row-relocation step simulation (`kappa` + `returnpc` migration) and divergence counterexample | `return_relocated_step_sim`, `copy_relocated_step_sim`, `unmigrated_returnpc_diverges` | `Pathlang/Control.lean:1627`, `Pathlang/Control.lean:1693`, `Pathlang/Control.lean:1790` |
| Invariant 2 (`inv:commit`): Commit-point completeness from `vthread.create` across steps and runs (+ `libSig` counterexample) | `vthreadCreate_commitComplete`, `CommitComplete.of_storeAgree`, `commitComplete_gatedStep`, `commitComplete_commitPointAt`, `commitComplete_resumableRun`, `commitComplete_broken_by_libSig_overwrite` | `Pathlang/Invariants.lean:182`, `Pathlang/Invariants.lean:268`, `Pathlang/Invariants.lean:1308`, `Pathlang/Invariants.lean:1344`, `Pathlang/Invariants.lean:1391`, `Pathlang/Invariants.lean:1411` |
| Proposition 6 (`prop:failstop`): Fail-stop on malformed control state (`sigma = error`, `mu` populated, data untouched) | `fail_stop`, `fail_stop_with_extensions` | `Pathlang/Invariants.lean:1843`, `Pathlang/Invariants.lean:1913` |
| Theorem 4 (`thm:crash`): Action-cut replay, `StepWarFree`, remaining-writes factorization, and commit-split crash dichotomy | `replay`, `crashAt_applyAll_remaining`, `stepWarFree_iff_forall_cuts`, `scoped_crash_recovery`, `scoped_crash_recovery_write_prefix`, `commitSplit_precommit_write_prefix_recovery`, `commitSplit_crash_dichotomy` | `Pathlang/Crash.lean:191`, `Pathlang/StepCrash.lean:72`, `Pathlang/StepCrash.lean:167`, `Pathlang/StepCrash.lean:196`, `Pathlang/StepCrash.lean:214`, `Pathlang/StepCrash.lean:280`, `Pathlang/StepCrash.lean:326` |
| Per-rule Crash Recovery: `Goto` and `Br` crash dichotomies | `goto_gatedStep_crash_dichotomy`, `br_gatedStep_crash_dichotomy` | `Pathlang/StepCrash.lean:872`, `Pathlang/StepCrash.lean:949` |
| Per-rule Crash Recovery: `Copy` non-aliased recovery, pointer self-alias counterexample, and staged-temporary recovery | `copy_rule_write_prefix_recovery`, `copy_gatedStep_crash_dichotomy`, `copy_ptr_self_alias_fails_replay`, `copy_staged_via_temp_recovery` | `Pathlang/StepCrash.lean:1053`, `Pathlang/StepCrash.lean:1114`, `Pathlang/StepCrash.lean:1232`, `Pathlang/StepCrash.lean:1328` |
| Per-rule Crash Recovery: Crash-ordered `Call` and `UserCall` (frame before `kappa`) and reversed-order counterexample | `bindChild_stepWarFree`, `call_gatedStep_crash_dichotomy`, `userCall_gatedStep_crash_dichotomy`, `reversed_call_crash_counterexample` | `Pathlang/StepCrash.lean:1607`, `Pathlang/StepCrash.lean:1629`, `Pathlang/StepCrash.lean:1769`, `Pathlang/StepCrash.lean:1935` |
| Per-rule Crash Recovery: Crash-ordered `Return` (`kappa` before `delWhere`), mid-`delWhere` `~`-equivalence, root `Return`, and reversed-order counterexample | `return_gatedStep_crash_dichotomy`, `return_mid_delWhere_next_step`, `returnRoot_stepWarFree`, `reversed_return_crash_counterexample` | `Pathlang/StepCrash.lean:2001`, `Pathlang/StepCrash.lean:2098`, `Pathlang/StepCrash.lean:2229`, `Pathlang/StepCrash.lean:2309` |
| Toy WAR-freedom examples: aliased increment and aliased call | `incr_not_A0`, `lowered_A0`, `aliased_not_A0`, `distinct_A0` | `Pathlang/Examples/AliasedIncrement.lean:69`, `Pathlang/Examples/AliasedIncrement.lean:141`, `Pathlang/Examples/AliasedCall.lean:96`, `Pathlang/Examples/AliasedCall.lean:113` |
