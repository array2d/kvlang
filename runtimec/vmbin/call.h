#ifndef VMBIN_CALL_H
#define VMBIN_CALL_H

/*
 * 组件：vmbin/call
 * 作用：按被调用 Program 的签名建立调用绑定并传递读写参数。
 * 联系：engine route 驱动调用；签名是别名策略的唯一来源。
 * 约束：调用不得重复已经提交的副作用。
 */

#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "vmbin/exit.h"
#include "vmbin/frame.h"
#include "vmbin/program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmbin_call_binding vmbin_call_binding_t;
typedef struct vmbin_call_request {
    uint32_t callsite_identifier;
    uint32_t return_logical_program_counter;
} vmbin_call_request_t;

runtimec_status_t vmbin_call_binding_create(
    const vmbin_frame_t *caller,
    const vmbin_program_t *callee,
    const vmbin_call_request_t *request,
    const runtimec_allocator_t *allocator,
    vmbin_call_binding_t **output_binding);
runtimec_status_t vmbin_call_binding_destroy(vmbin_call_binding_t *binding);
runtimec_status_t vmbin_call_enter(
    vmbin_frame_t *caller,
    vmbin_frame_t *callee,
    const vmbin_call_binding_t *binding,
    vmbin_exit_t *output_exit);
runtimec_status_t vmbin_call_return(
    vmbin_frame_t *caller,
    const vmbin_frame_t *callee,
    const vmbin_call_binding_t *binding,
    vmbin_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
