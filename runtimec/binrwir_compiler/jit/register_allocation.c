/* JIT 专属寄存器分配的失败封闭骨架。 */

#include "binrwir_compiler/jit/register_allocation.h"

runtimec_status_t binrwir_jit_register_allocation_create(
    const binrwir_jit_verified_program_t *verified_program,
    const binrwir_execution_plan_t *execution_plan,
    const binrwir_jit_target_t *target,
    const runtimec_allocator_t *allocator,
    binrwir_jit_register_allocation_t **output_allocation,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_allocation != NULL) {
        *output_allocation = NULL;
    }
    if (refusal != NULL) {
        *refusal = (binrwir_jit_refusal_t){
            .reason = BINRWIR_JIT_REFUSAL_INVALID_PROGRAM,
        };
    }
    if (verified_program == NULL || verified_program->program == NULL ||
        execution_plan == NULL || target == NULL ||
        !runtimec_allocator_is_valid(allocator) || output_allocation == NULL ||
        refusal == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    refusal->reason = BINRWIR_JIT_REFUSAL_NONE;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_register_allocation_get_view(
    const binrwir_jit_register_allocation_t *allocation,
    binrwir_jit_register_allocation_view_t *output_view) {
    if (output_view != NULL) {
        *output_view = (binrwir_jit_register_allocation_view_t){0};
    }
    if (allocation == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_register_allocation_destroy(
    binrwir_jit_register_allocation_t *allocation) {
    if (allocation == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
