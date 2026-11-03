#ifndef VMBIN_CALL_H
#define VMBIN_CALL_H

/*
 * 组件：vmbin/call
 * 作用：按 canonical BinRWIR Program 的签名建立控制流调用关系。
 * 联系：engine route 驱动调用；指令中的 argaddr 是参数 V.body 的唯一位置。
 * 约束：调用对象只能保存控制转移元数据，不得保存 V.body 内容、Cell、value
 *       slot 或第二份地址缓存；调用不得重复已经提交的副作用。
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

typedef struct vmbin_call_control vmbin_call_control_t;
typedef struct vmbin_call_request {
    uint32_t call_instruction_identifier;
    uint32_t return_logical_program_counter;
} vmbin_call_request_t;

runtimec_status_t vmbin_call_control_create(
    const binrwir_execution_frame_t *caller,
    const binrwir_program_t *callee,
    const vmbin_call_request_t *request,
    const runtimec_allocator_t *allocator,
    vmbin_call_control_t **output_control);
runtimec_status_t vmbin_call_control_destroy(vmbin_call_control_t *control);
runtimec_status_t vmbin_call_enter(
    binrwir_execution_frame_t *caller,
    binrwir_execution_frame_t *callee,
    const vmbin_call_control_t *control,
    binrwir_execution_exit_t *output_exit);
/* 返回：释放绑定表并把被调帧还原成「没进过调用」的状态，帧可再次进入。 */
runtimec_status_t vmbin_call_return(
    binrwir_execution_frame_t *caller, binrwir_execution_frame_t *callee,
    const vmbin_call_control_t *control,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
