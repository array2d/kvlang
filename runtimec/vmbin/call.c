/*
 * 组件：vmbin/call 的实现
 * 作用：调用绑定与进出 Frame 的失败桩。
 * 约束：未实现必须同时返回结构化错误和未实现状态。
 */

#include "vmbin/call.h"

static void vmbin_call_initialize_error_exit(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
    output_exit->resume_logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
}

runtimec_status_t vmbin_call_binding_create(
    const binrwir_execution_frame_t *caller,
    const binrwir_program_t *callee,
    const vmbin_call_request_t *request,
    const runtimec_allocator_t *allocator,
    vmbin_call_binding_t **output_binding) {
    (void)caller;
    (void)callee;
    (void)request;
    (void)allocator;
    if (output_binding != NULL) {
        *output_binding = NULL;
    }
    if (caller == NULL || callee == NULL || request == NULL ||
        request->callsite_identifier == BINRWIR_IDENTIFIER_NONE ||
        allocator == NULL || output_binding == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t vmbin_call_binding_destroy(vmbin_call_binding_t *binding) {
    if (binding == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t vmbin_call_enter(
    binrwir_execution_frame_t *caller,
    binrwir_execution_frame_t *callee,
    const vmbin_call_binding_t *binding,
    binrwir_execution_exit_t *output_exit) {
    if (output_exit != NULL) {
        vmbin_call_initialize_error_exit(output_exit,
                                         RUNTIMEC_STATUS_INVALID);
    }
    if (caller == NULL || callee == NULL || binding == NULL ||
        output_exit == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    vmbin_call_initialize_error_exit(output_exit,
                                     RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t vmbin_call_return(
    binrwir_execution_frame_t *caller,
    const binrwir_execution_frame_t *callee,
    const vmbin_call_binding_t *binding,
    binrwir_execution_exit_t *output_exit) {
    if (output_exit != NULL) {
        vmbin_call_initialize_error_exit(output_exit,
                                         RUNTIMEC_STATUS_INVALID);
    }
    if (caller == NULL || callee == NULL || binding == NULL ||
        output_exit == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    vmbin_call_initialize_error_exit(output_exit,
                                     RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
