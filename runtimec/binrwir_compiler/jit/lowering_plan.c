/* binrwir_compiler/jit/lowering_plan 的失败封闭骨架。 */

#include "binrwir_compiler/jit/lowering_plan.h"

runtimec_status_t binrwir_jit_lowering_plan_create(
    const binrwir_jit_verified_program_t *verified_program,
    const binrwir_execution_plan_t *execution_plan,
    const binrwir_jit_target_t *target,
    const binrwir_jit_lowering_plan_options_t *options,
    const runtimec_allocator_t *allocator,
    binrwir_jit_lowering_plan_t **output_plan,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_plan != NULL) {
        *output_plan = NULL;
    }
    if (refusal != NULL) {
        *refusal = (binrwir_jit_refusal_t){
            .reason = BINRWIR_JIT_REFUSAL_INVALID_PROGRAM,
        };
    }
    if (verified_program == NULL || verified_program->program == NULL ||
        execution_plan == NULL || target == NULL || options == NULL ||
        options->structure_size != sizeof(*options) ||
        !runtimec_allocator_is_valid(allocator) || output_plan == NULL ||
        refusal == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    refusal->reason = BINRWIR_JIT_REFUSAL_NONE;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_lowering_plan_destroy(binrwir_jit_lowering_plan_t *plan) {
    if (plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_lowering_plan_get_register_allocation(
    const binrwir_jit_lowering_plan_t *plan,
    const binrwir_jit_register_allocation_t **output_allocation) {
    if (output_allocation != NULL) {
        *output_allocation = NULL;
    }
    if (plan == NULL || output_allocation == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_lowering_plan_get_safepoint_plan(
    const binrwir_jit_lowering_plan_t *plan,
    const binrwir_jit_safepoint_plan_t **output_safepoint_plan) {
    if (output_safepoint_plan != NULL) {
        *output_safepoint_plan = NULL;
    }
    if (plan == NULL || output_safepoint_plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
