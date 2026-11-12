
#include "binrwir_compiler/jit/safepoint.h"

runtimec_status_t binrwir_jit_safepoint_plan_create(
    const binrwir_jit_verified_program_t *verified_program,
    const binrwir_jit_register_allocation_t *register_allocation,
    const runtimec_allocator_t *allocator,
    binrwir_jit_safepoint_plan_t **output_plan,
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
        register_allocation == NULL ||
        !runtimec_allocator_is_valid(allocator) || output_plan == NULL ||
        refusal == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    refusal->reason = BINRWIR_JIT_REFUSAL_NONE;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_safepoint_plan_get_view(
    const binrwir_jit_safepoint_plan_t *plan,
    binrwir_jit_safepoint_plan_view_t *output_view) {
    if (output_view != NULL) {
        *output_view = (binrwir_jit_safepoint_plan_view_t){0};
    }
    if (plan == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

static int binrwir_jit_safepoint_is_commit_boundary(
    binrwir_jit_safepoint_boundary_t boundary) {
    return boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION ||
           boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_PAUSE ||
           boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_HANDOFF ||
           boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_CALL ||
           boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_EXTERNAL_OBSERVATION;
}

static int binrwir_jit_safepoint_is_discard_boundary(
    binrwir_jit_safepoint_boundary_t boundary) {
    return boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_RECOVERY ||
           boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_RESUME ||
           boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_GUARD_FAILURE ||
           boundary == BINRWIR_JIT_SAFEPOINT_BOUNDARY_ERROR;
}

runtimec_status_t binrwir_jit_safepoint_commit_completed_state(
    const binrwir_jit_safepoint_plan_t *plan,
    binrwir_jit_safepoint_boundary_t boundary,
    uint32_t completed_logical_program_counter,
    uint32_t next_logical_program_counter,
    void *execution_state,
    binrwir_binvthread_frame_t *frame,
    runtimec_error_t *error) {
    (void)next_logical_program_counter;
    (void)error;
    if (plan == NULL || !binrwir_jit_safepoint_is_commit_boundary(boundary) ||
        execution_state == NULL || frame == NULL ||
        frame->logical_program_counter != completed_logical_program_counter) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_safepoint_discard_incomplete_state(
    const binrwir_jit_safepoint_plan_t *plan,
    binrwir_jit_safepoint_boundary_t boundary,
    uint32_t current_logical_program_counter,
    void *execution_state,
    binrwir_binvthread_frame_t *frame,
    runtimec_error_t *error) {
    (void)error;
    if (plan == NULL || !binrwir_jit_safepoint_is_discard_boundary(boundary) ||
        execution_state == NULL || frame == NULL ||
        frame->logical_program_counter != current_logical_program_counter) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_safepoint_plan_destroy(
    binrwir_jit_safepoint_plan_t *plan) {
    if (plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
