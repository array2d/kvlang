#ifndef RUNTIMEC_ENGINE_SESSION_H
#define RUNTIMEC_ENGINE_SESSION_H

/*
 * 组件：engine/session
 * 作用：显式 owner graph 的根：host、vmkv encoder、BinRWIR Compiler、
 *       compilation repository、frame/register/view 池、configuration、
 *       generation 与 quiescent point。JIT compiler 与机器码存储完全封装在
 *       BinRWIR Compiler 内，不是 session 的直接 owner。禁止用
 *       function-static、global 或 thread-local storage 隐藏这些 owner。
 * 联系：公共接口层创建 session；route 在 session 上执行；多
 *       session/测试隔离都从本对象出发。
 * 约束：当前 scaffold 不宣称 handle 线程安全；并发协议必须由 session
 *       的锁序与 generation/lease 设计提供。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "contract/allocator.h"
#include "contract/host_services.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_session_t runtimec_session_t;
typedef struct runtimec_session_options_t {
    uint32_t structure_size;
    runtimec_allocator_t allocator;
    size_t compilation_cache_capacity;
    size_t machine_code_capacity;
    bool enable_jit;
    bool observe_every_instruction;
} runtimec_session_options_t;
runtimec_status_t runtimec_session_create(
    const runtimec_host_services_t *host_services,
    const runtimec_session_options_t *options,
    runtimec_session_t **output_session,
    runtimec_error_t *error);
runtimec_status_t runtimec_session_destroy(runtimec_session_t *session);
runtimec_status_t runtimec_session_get_host_services(
    runtimec_session_t *session,
    const runtimec_host_services_t **output_host_services);
runtimec_status_t runtimec_session_clear_derived_state(
    runtimec_session_t *session);

#ifdef __cplusplus
}
#endif

#endif
