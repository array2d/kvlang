#ifndef RUNTIMEC_ENGINE_PROGRAM_REPOSITORY_H
#define RUNTIMEC_ENGINE_PROGRAM_REPOSITORY_H

/*
 * 组件：engine/program_repository
 * 作用：Program 缓存与租约：find 只查询不可变 Program；insert 只接收
 *       route 已完成编译的 Program；release 归还 lease；invalidate/clear 按统一失效
 *       模型撤销新获取资格，但不得释放仍被 lease pin 的 Program。cache key
 *       至少含 store 实例、函数、source revision、
 *       schema、instruction set architecture、compiler revision 与
 *       BinRWIR binary interface 世代。
 * 联系：route 在编译前查询，并在 vmkv 编译后插入；lease 保证解释、vmjit、
 *       call binding 期间 Program 不被回收。
 * 约束：Program 内容归 vmbin；repository 只持有 lease 与索引，绝不调用 vmkv。
 */

#include "contract/allocator.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "vmbin/program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_program_repository_t runtimec_program_repository_t;
typedef struct runtimec_program_repository_key_t {
    runtimec_snapshot_t snapshot;
    uint32_t rwir_manifest_version;
    uint32_t binrwir_binary_interface_generation;
    uint32_t compiler_revision;
    uint32_t reserved;
} runtimec_program_repository_key_t;
typedef enum runtimec_program_lookup_result_t {
    RUNTIMEC_PROGRAM_LOOKUP_INVALID = 0,
    RUNTIMEC_PROGRAM_LOOKUP_FOUND = 1,
    RUNTIMEC_PROGRAM_LOOKUP_NOT_FOUND = 2
} runtimec_program_lookup_result_t;
typedef struct runtimec_program_lease_t {
    const vmbin_program_t *program;
    runtimec_snapshot_t snapshot;
    void *token;
} runtimec_program_lease_t;
runtimec_status_t runtimec_program_repository_create(
    runtimec_allocator_t allocator,
    size_t capacity,
    runtimec_program_repository_t **output_repository,
    runtimec_error_t *error);
runtimec_status_t runtimec_program_repository_destroy(
    runtimec_program_repository_t *repository);
runtimec_status_t runtimec_program_repository_find(
    runtimec_program_repository_t *repository,
    const runtimec_program_repository_key_t *key,
    runtimec_program_lookup_result_t *output_lookup_result,
    runtimec_program_lease_t *output_lease,
    runtimec_error_t *error);
runtimec_status_t runtimec_program_repository_insert(
    runtimec_program_repository_t *repository,
    const runtimec_program_repository_key_t *key,
    vmbin_program_t *program,
    runtimec_program_lease_t *output_lease,
    runtimec_error_t *error);
runtimec_status_t runtimec_program_repository_release(
    runtimec_program_repository_t *repository,
    runtimec_program_lease_t *lease);
runtimec_status_t runtimec_program_repository_invalidate(
    runtimec_program_repository_t *repository,
    const runtimec_snapshot_t *snapshot);
runtimec_status_t runtimec_program_repository_clear(
    runtimec_program_repository_t *repository);

#ifdef __cplusplus
}
#endif

#endif
