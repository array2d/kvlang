#ifndef RUNTIMEC_VMJIT_CACHE_H
#define RUNTIMEC_VMJIT_CACHE_H

/*
 * 组件：vmjit/cache
 * 作用：代码缓存的 key 与 get_or_compile：key 至少包含 Program snapshot/
 *       generation、执行 binary interface、架构、processor feature、tier、
 *       code generation revision 与 vmjit generation；
 *       活动 frame/lease 必须 pin 住
 *       Program/code，新 generation 不得热替换仍执行的旧页。
 * 联系：engine/session 拥有 code cache；vmjit/compiler 生成 code；
 *       invalidate 由统一失效模型触发。
 * 约束：negative cache 与 retired list 是可删除 sidecar；cache 不是
 *       Program 的事实源。
 */

#include "contract/allocator.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "vmjit/compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmjit_code_cache_t vmjit_code_cache_t;
typedef struct vmjit_cache_key_t {
    runtimec_snapshot_t snapshot;
    uint16_t execution_binary_interface_major_version;
    uint16_t optimization_tier;
    uint32_t code_generation_revision;
    uint64_t feature_mask;
    uint64_t vmjit_generation;
} vmjit_cache_key_t;
runtimec_status_t vmjit_code_cache_create(
    runtimec_allocator_t allocator,
    size_t capacity,
    vmjit_code_cache_t **output_cache,
    runtimec_error_t *error);
runtimec_status_t vmjit_code_cache_destroy(vmjit_code_cache_t *cache);
runtimec_status_t vmjit_code_cache_clear(vmjit_code_cache_t *cache);
runtimec_status_t vmjit_code_cache_invalidate(
    vmjit_code_cache_t *cache,
    const runtimec_snapshot_t *snapshot);
runtimec_status_t vmjit_code_cache_get_or_compile(
    vmjit_code_cache_t *cache,
    vmjit_compiler_t *compiler,
    const vmjit_cache_key_t *key,
    const vmjit_verified_program_t *verified_program,
    vmjit_code_t **output_code,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
