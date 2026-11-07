#ifndef RUNTIMEC_ENGINE_PLATFORM_SESSION_H
#define RUNTIMEC_ENGINE_PLATFORM_SESSION_H

/*
 * 组件：engine/platform_session
 * 作用：把「一个 DSN」变成可用的 session：按 SONAME 装载 kvspace 前端、
 *       绑定符号表、连接后端、建立 host 能力表、创建 session，并持有这条
 *       链上全部资源的唯一所有权。
 * 联系：engine 是唯一允许同时依赖 host 与非 host 执行组件的层，因此引导放在
 *       这里；interface 只调用本组件，不直接接触 kvspace。
 * 约束：不读取任何库路径类环境变量（仓库只有 KVSPACE 一个运行时环境变量，
 *       由调用方把 DSN 传进来）；装载失败必须点名找不到的 SONAME，不静默降级；
 *       任一环节失败都按逆序释放已建资源，不留下半初始化句柄。
 */

#include <stdbool.h>
#include <stddef.h>

#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/status.h"
#include "engine/session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_platform_session_t runtimec_platform_session_t;

/* 进程默认分配器（malloc/free）；供公共接口层填充 session 选项。 */
runtimec_allocator_t runtimec_default_allocator(void);

/*
 * 从 data_source_name（如 `shm:///tmp/store.shm`、`redis://host:port`）建立
 * 一条完整链路。成功时输出持有 session；失败时输出为 NULL 且已释放全部中间
 * 资源，error 里给出可定位的原因。
 */
runtimec_status_t runtimec_platform_session_create(
    const char *data_source_name, uint64_t instance_identifier,
    const runtimec_session_options_t *options,
    runtimec_platform_session_t **output, runtimec_error_t *error);

/* 释放整条链路：session → host 能力表 → kvspace 句柄 → 库句柄。 */
runtimec_status_t runtimec_platform_session_destroy(
    runtimec_platform_session_t *platform_session, runtimec_error_t *error);

/* 借用内部 session；平台会话销毁后指针失效。 */
runtimec_status_t runtimec_platform_session_get_session(
    const runtimec_platform_session_t *platform_session,
    runtimec_session_t **output_session, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
