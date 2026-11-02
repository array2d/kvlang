/*
 * 组件：vmbin/call 的实现
 * 作用：调用绑定与进出 Frame 的失败桩。
 * 约束：未实现必须同时返回结构化错误和未实现状态。
 */

#include "vmbin/call.h"

static runtimec_status_t vmbin_call_unimplemented(
    vmbin_exit_t *output_exit) {
    if (output_exit != NULL) {
        *output_exit = (vmbin_exit_t){0};
        output_exit->kind = VMBIN_EXIT_ERROR;
        output_exit->status = RUNTIMEC_STATUS_NOT_IMPLEMENTED;
        output_exit->logical_program_counter = VMBIN_PROGRAM_COUNTER_NONE;
        output_exit->resume_logical_program_counter =
            VMBIN_PROGRAM_COUNTER_NONE;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_call_binding_create(
    const vmbin_frame_t *caller,
    const vmbin_program_t *callee,
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
        request->callsite_identifier == VMBIN_IDENTIFIER_NONE ||
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
    vmbin_frame_t *caller,
    vmbin_frame_t *callee,
    const vmbin_call_binding_t *binding,
    vmbin_exit_t *output_exit) {
    runtimec_status_t status = vmbin_call_unimplemented(output_exit);
    if (caller == NULL || callee == NULL || binding == NULL ||
        output_exit == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return status;
}
runtimec_status_t vmbin_call_return(
    vmbin_frame_t *caller,
    const vmbin_frame_t *callee,
    const vmbin_call_binding_t *binding,
    vmbin_exit_t *output_exit) {
    runtimec_status_t status = vmbin_call_unimplemented(output_exit);
    if (caller == NULL || callee == NULL || binding == NULL ||
        output_exit == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return status;
}
