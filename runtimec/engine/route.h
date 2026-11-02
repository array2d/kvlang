#ifndef RUNTIMEC_ENGINE_ROUTE_H
#define RUNTIMEC_ENGINE_ROUTE_H

/*
 * 组件：engine/route
 * 作用：唯一执行策略所有者：取权威快照 -> 查 compilation repository ->
 *       未命中则 vmkv 等价编码 canonical BinRWIR -> BinRWIR Compiler 编译 ->
 *       执行前读取 binrwir_execution_target_t ->
 *       BINRWIR_EXECUTION_TARGET_JIT 只调用 binrwir_jit_execute，VMBIN
 *       目标只调用 vmbin_execute -> 只提交成功指令的
 *       effects/checkpoint -> 观察 status/source generation -> 继续、暂停、
 *       handoff、done、invalidate 或 error。
 * 联系：只有 route.c 同时包含 vmkv、vmbin 与 BinRWIR JIT；host port 只经本层
 *       提交 durable effect；公共接口层把请求翻译成本层请求。
 * 约束：NOT_COMPILABLE 是已完成的编译判定并选择 VMBIN；ERROR 直接失败。
 *       执行期不重新选择目标。其它文件不得绕过 route 自行选择执行器或
 *       提交 checkpoint。
 */

#include <stddef.h>
#include <stdint.h>
#include "contract/status.h"
#include "contract/view.h"
#include "engine/session.h"
#include "binrwir/execution_exit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_route_kind_t {
    RUNTIMEC_ROUTE_FUNCTION = 0,
    RUNTIMEC_ROUTE_PROGRAM_COUNTER = 1,
    RUNTIMEC_ROUTE_VTHREAD = 2,
    RUNTIMEC_ROUTE_BOOTSTRAP = 3
} runtimec_route_kind_t;
typedef struct runtimec_route_request_t {
    runtimec_route_kind_t kind;
    runtimec_string_view_t target;
    const runtimec_string_view_t *arguments;
    size_t argument_count;
    uint64_t instruction_budget;
} runtimec_route_request_t;
typedef struct runtimec_route_result_t {
    runtimec_status_t status;
    binrwir_execution_exit_t execution_exit;
    runtimec_string_view_t return_value;
} runtimec_route_result_t;
runtimec_status_t runtimec_route_execute(
    runtimec_session_t *session,
    const runtimec_route_request_t *request,
    runtimec_route_result_t *output_result,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
