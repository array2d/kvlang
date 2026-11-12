#ifndef VMBIN_CALL_H
#define VMBIN_CALL_H

#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/binfunc.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmbin_call_control vmbin_call_control_t;
typedef struct vmbin_call_request {
    uint32_t call_instruction_identifier;
    uint32_t return_logical_program_counter;
} vmbin_call_request_t;

runtimec_status_t vmbin_call_control_create(
    const binrwir_binvthread_frame_t *caller,
    const binrwir_binfunc_t *callee,
    const vmbin_call_request_t *request,
    const runtimec_allocator_t *allocator,
    vmbin_call_control_t **output_control);
runtimec_status_t vmbin_call_control_destroy(vmbin_call_control_t *control);
runtimec_status_t vmbin_call_enter(
    binrwir_binvthread_frame_t *caller,
    binrwir_binvthread_frame_t *callee,
    const vmbin_call_control_t *control,
    binrwir_execution_exit_t *output_exit);

runtimec_status_t vmbin_call_return(
    binrwir_binvthread_frame_t *caller, binrwir_binvthread_frame_t *callee,
    const vmbin_call_control_t *control,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
