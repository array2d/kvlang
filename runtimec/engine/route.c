/*
 * 组件：engine/route 的实现
 * 作用：唯一执行策略入口；当前 fail-closed，返回 NOT_IMPLEMENTED 并
 *       把结构化 exit 标为 ERROR。
 * 联系：这是唯一同时看到 vmkv、vmbin 与 vmjit 的文件，并且只有这里
 *       可以选择执行路径和提交 checkpoint。
 */

#include "engine/route.h"
#include "vmbin/interpreter.h"
#include "vmjit/vmjit.h"
#include "vmkv/compiler.h"

runtimec_status_t runtimec_route_execute(
    runtimec_session_t *session,
    const runtimec_route_request_t *request,
    runtimec_route_result_t *output_result,
    runtimec_error_t *error) {
    (void)session;
    (void)request;
    (void)error;
    if (output_result != NULL) {
        *output_result = (runtimec_route_result_t){0};
        output_result->status = RUNTIMEC_STATUS_INVALID;
        output_result->exit.status = RUNTIMEC_STATUS_INVALID;
        output_result->exit.kind = VMBIN_EXIT_ERROR;
    }
    if (session == NULL || request == NULL || output_result == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_result->status = RUNTIMEC_STATUS_NOT_IMPLEMENTED;
    output_result->exit.status = RUNTIMEC_STATUS_NOT_IMPLEMENTED;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
