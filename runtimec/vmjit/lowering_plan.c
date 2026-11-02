/* vmjit/lowering_plan 的失败封闭骨架。 */

#include "vmjit/lowering_plan.h"

runtimec_status_t vmjit_lowering_plan_create(
    const vmjit_verified_program_t *verified_program,
    const binrwir_execution_plan_t *execution_plan,
    const vmjit_target_t *target,
    const vmjit_lowering_plan_options_t *options,
    const runtimec_allocator_t *allocator,
    vmjit_lowering_plan_t **output_plan,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_plan != NULL) {
        *output_plan = NULL;
    }
    if (refusal != NULL) {
        *refusal = (vmjit_refusal_t){
            .reason = VMJIT_REFUSAL_INVALID_PROGRAM,
        };
    }
    if (verified_program == NULL || verified_program->program == NULL ||
        execution_plan == NULL || target == NULL || options == NULL ||
        options->structure_size != sizeof(*options) ||
        !runtimec_allocator_is_valid(allocator) || output_plan == NULL ||
        refusal == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    refusal->reason = VMJIT_REFUSAL_NONE;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_lowering_plan_destroy(vmjit_lowering_plan_t *plan) {
    if (plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
