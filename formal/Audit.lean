import Pathlang

/-! Run `lake env lean Audit.lean`. Each line must list only Lean's standard
axioms (`propext`, `Classical.choice`, `Quot.sound`) or none; `sorryAx` would
mean an unfinished proof. -/

#print axioms Pathlang.applyAll_congr
#print axioms Pathlang.applyAll_absorb
#print axioms Pathlang.applyAll_take_absorb
#print axioms Pathlang.exec_congr
#print axioms Pathlang.after_congr
#print axioms Pathlang.WarFreeAt.mono
#print axioms Pathlang.warFreeAt_of_reads
#print axioms Pathlang.replay
#print axioms Pathlang.Examples.AliasedIncrement.incr_not_A0
#print axioms Pathlang.Examples.AliasedIncrement.incr_not_warFree
#print axioms Pathlang.Examples.AliasedIncrement.lowered_A0
#print axioms Pathlang.Examples.AliasedCall.aliased_not_A0
#print axioms Pathlang.Examples.AliasedCall.aliased_not_warFree
#print axioms Pathlang.Examples.AliasedCall.distinct_A0
#print axioms Pathlang.exec_bind
#print axioms Pathlang.after_bind
#print axioms Pathlang.ruleGuard_unique
#print axioms Pathlang.gatedRel_iff_exec
#print axioms Pathlang.gatedRel_deterministic
#print axioms Pathlang.core_invariance
#print axioms Pathlang.gatedStep_footprint
#print axioms Pathlang.gatedContinuation_right_mover
#print axioms Pathlang.theorem1_v2_reduction
#print axioms Pathlang.stop_takes_effect_at_next_gate
#print axioms Pathlang.midstep_stop_pauses_next_gate
#print axioms Pathlang.StoreAgree.refl
#print axioms Pathlang.StoreAgree.symm
#print axioms Pathlang.StoreAgree.trans
#print axioms Pathlang.lease_mutual_exclusion
#print axioms Pathlang.StepDiscipline.of_storeAgree
#print axioms Pathlang.step_respects_agree
#print axioms Pathlang.stop_resume_resumption
#print axioms Pathlang.stop_resume_isPrefix
#print axioms Pathlang.stop_resume_completed

#print axioms Pathlang.gatedStep_unread
#print axioms Pathlang.gatedStep_unread_strict
#print axioms Pathlang.uninterrupted_unread
#print axioms Pathlang.stop_edit_resume_det
#print axioms Pathlang.stop_edit_resume_unread_prefix
#print axioms Pathlang.stop_edit_resume_unread
#print axioms Pathlang.live_edit_spliced_program_equiv
#print axioms Pathlang.return_relocated_step_sim
#print axioms Pathlang.copy_relocated_step_sim
#print axioms Pathlang.unmigrated_returnpc_diverges

#print axioms Pathlang.vthreadCreate_validCommitPoint
#print axioms Pathlang.vthreadCreate_commitComplete
#print axioms Pathlang.CommitComplete.of_storeAgree
#print axioms Pathlang.commitComplete_stepCore_ok
#print axioms Pathlang.commitComplete_gatedStep
#print axioms Pathlang.commitComplete_commitPointAt
#print axioms Pathlang.commitComplete_resumableRun
#print axioms Pathlang.commitComplete_broken_by_libSig_overwrite
#print axioms Pathlang.fail_stop
#print axioms Pathlang.fail_stop_with_extensions

#print axioms Pathlang.crashAt_applyAll_remaining
#print axioms Pathlang.stepWarFree_iff_forall_cuts
#print axioms Pathlang.scoped_crash_recovery
#print axioms Pathlang.scoped_crash_recovery_write_prefix
#print axioms Pathlang.commitSplit_precommit_write_prefix_recovery
#print axioms Pathlang.commitSplit_crash_dichotomy
#print axioms Pathlang.goto_gatedStep_crash_dichotomy
#print axioms Pathlang.br_gatedStep_crash_dichotomy
#print axioms Pathlang.copy_rule_write_prefix_recovery
#print axioms Pathlang.copy_gatedStep_crash_dichotomy
#print axioms Pathlang.copy_ptr_self_alias_fails_replay
#print axioms Pathlang.copy_staged_via_temp_recovery
#print axioms Pathlang.bindChild_stepWarFree
#print axioms Pathlang.call_gatedStep_crash_dichotomy
#print axioms Pathlang.userCall_gatedStep_crash_dichotomy
#print axioms Pathlang.reversed_call_crash_counterexample
#print axioms Pathlang.return_gatedStep_crash_dichotomy
#print axioms Pathlang.return_mid_delWhere_next_step
#print axioms Pathlang.returnRoot_stepWarFree
#print axioms Pathlang.reversed_return_crash_counterexample
#print axioms Pathlang.gatedStep_locality_no_ext
#print axioms Pathlang.gatedStep_locality
