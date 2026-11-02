#ifndef VMBIN_CALL_H
#define VMBIN_CALL_H

/*
 * 组件：vmbin/call
 * 作用：按 canonical BinRWIR Program 的签名建立调用绑定并传递读写参数。
 * 联系：engine route 驱动调用；签名是别名策略的唯一来源。
 * 约束：调用不得重复已经提交的副作用。
 */

#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/program.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmbin_call_binding vmbin_call_binding_t;
typedef struct vmbin_call_request {
    uint32_t callsite_identifier;
    uint32_t return_logical_program_counter;
} vmbin_call_request_t;

runtimec_status_t vmbin_call_binding_create(
    const binrwir_execution_frame_t *caller,
    const binrwir_program_t *callee,
    const vmbin_call_request_t *request,
    const runtimec_allocator_t *allocator,
    vmbin_call_binding_t **output_binding);
runtimec_status_t vmbin_call_binding_destroy(vmbin_call_binding_t *binding);
runtimec_status_t vmbin_call_enter(
    binrwir_execution_frame_t *caller,
    binrwir_execution_frame_t *callee,
    const vmbin_call_binding_t *binding,
    binrwir_execution_exit_t *output_exit);
runtimec_status_t vmbin_call_return(
    binrwir_execution_frame_t *caller,
    const binrwir_execution_frame_t *callee,
    const vmbin_call_binding_t *binding,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
