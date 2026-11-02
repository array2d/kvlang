/* vmjit/lower 的失败封闭骨架。 */

#include "vmjit/lower.h"

runtimec_status_t vmjit_lower(
    const vmjit_lowering_plan_t *plan,
    const runtimec_allocator_t *allocator,
    vmjit_lowered_program_t **output_program,
    runtimec_error_t *error) {
    (void)error;
    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (plan == NULL || !runtimec_allocator_is_valid(allocator) ||
        output_program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_lowered_program_destroy(
    vmjit_lowered_program_t *program) {
    if (program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
