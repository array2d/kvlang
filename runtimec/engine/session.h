#ifndef RUNTIMEC_ENGINE_SESSION_H
#define RUNTIMEC_ENGINE_SESSION_H

/*
 * 组件：engine/session
 * 作用：显式 owner graph 的根：host services、vmkv encoder、BinRWIR
 *       Compiler、compilation repository 与 configuration。JIT compiler 与
 *       机器码存储完全封装在 BinRWIR Compiler 内，不是 session 的直接 owner。
 *       BinRWIR/VMBIN 不拥有额外的执行状态池或 V.body cache。禁止用
 *       function-static、global 或 thread-local storage 隐藏 owner。
 * 联系：公共接口层创建 session；route 在 session 上执行；多
 *       session/测试隔离都从本对象出发。
 * 约束：每条逻辑 instruction 完成前写回全部 V.body，并建立恢复边界，这是
 *       无条件语义，不由 session option 开关控制。当前 scaffold 不宣称 handle
 *       线程安全；并发协议必须由 session 的锁序与 generation/lease 设计提供。
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
    /*
     * kvspace 句柄（`kvspaceConnect` 的原样返回值）。**调用方 connect 并拥有**，
     * session 只借用、不关闭——关闭时机属于 connect 的那一方。
     *
     * 为什么走 options 而不是 create 参数：session 不发明 DSN，也不该替调用方
     * 决定后端；这里只收一个已经打开的句柄。可以为 NULL（不接存储的用例）。
     */
    void *kvspace_handle;
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
/*
 * 借出调用方给的 kvspace 句柄（`options.kvspace_handle` 的原样透传）。
 * 句柄可以为 NULL：表示本 session 不接存储，此时返回 SUCCESS 且输出为 NULL。
 */
runtimec_status_t runtimec_session_get_kvspace_handle(
    runtimec_session_t *session, void **output_kvspace_handle);
runtimec_status_t runtimec_session_clear_derived_state(
    runtimec_session_t *session);

/*
 * route 需要的运行期组件，一次借出。session 仍是唯一 owner：这些指针只在
 * session 销毁之前有效，调用者不得销毁或替换它们。builtin 注册表本阶段为零
 * 条目表——没有 builtin 实现，走 route 的 builtin 指令因此失败封闭。
 *
 * 只前置声明、不 include 各组件头：`check_layers` 允许 session 作为 owner
 * graph 的根同时持有两个策略层，但分派仍然只有 route 一处。
 */
typedef struct vmkv_encoder_t vmkv_encoder_t;
typedef struct binrwir_compiler_t binrwir_compiler_t;
typedef struct runtimec_compilation_repository_t
    runtimec_compilation_repository_t;
typedef struct binrwir_registry binrwir_registry_t;

typedef struct runtimec_session_runtime_components_t {
    vmkv_encoder_t *vmkv_encoder;
    binrwir_compiler_t *binrwir_compiler;
    runtimec_compilation_repository_t *compilation_repository;
    const binrwir_registry_t *builtin_registry;
} runtimec_session_runtime_components_t;

runtimec_status_t runtimec_session_get_runtime_components(
    runtimec_session_t *session,
    runtimec_session_runtime_components_t *output_components);

/*
 * checkpoint 的单调提交序号：取号即前进，session 销毁前一直递增。route 不在
 * 自己身上存状态，序号只能由 owner graph 的根给出。
 */
runtimec_status_t runtimec_session_take_checkpoint_sequence(
    runtimec_session_t *session, uint64_t *output_sequence);

#ifdef __cplusplus
}
#endif

#endif
