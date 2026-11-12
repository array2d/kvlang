
#include "binrwir_compiler/jit/lower.h"

runtimec_status_t binrwir_jit_lower(
    const binrwir_jit_lowering_plan_t *plan,
    const runtimec_allocator_t *allocator,
    binrwir_jit_lowered_program_t **output_program,
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

runtimec_status_t binrwir_jit_lowered_program_destroy(
    binrwir_jit_lowered_program_t *program) {
    if (program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
