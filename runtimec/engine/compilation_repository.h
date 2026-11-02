#ifndef RUNTIMEC_ENGINE_COMPILATION_REPOSITORY_H
#define RUNTIMEC_ENGINE_COMPILATION_REPOSITORY_H

/*
 * 组件：engine/compilation_repository
 * 作用：缓存完整 BinRWIR compilation 并提供租约。find 只查询不可变
 *       compilation；insert 只接收 BinRWIR Compiler 已完成的 compilation；
 *       release 归还 lease；invalidate/clear 撤销新获取资格，但不得释放仍被
 *       lease 持有的 compilation。
 * 联系：route 在编码和编译前查询，在 BinRWIR Compiler 完成后插入；lease
 *       保证 canonical Program、执行计划与可选机器码在执行期间同时有效。
 * 约束：repository 只持有 compilation 的所有权、租约与索引，不调用 vmkv、
 *       vmbin 或 BinRWIR JIT，也不改变已确定的执行目标。
 */

#include "binrwir_compiler/compiler.h"
#include "contract/allocator.h"
#include "contract/snapshot.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_compilation_repository_t
    runtimec_compilation_repository_t;

typedef struct runtimec_compilation_repository_key_t {
    runtimec_snapshot_t snapshot;
    uint32_t rwir_manifest_version;
    uint32_t canonical_encoding_revision;
    uint32_t binrwir_binary_interface_generation;
    uint32_t compiler_revision;
} runtimec_compilation_repository_key_t;

/*
 * find 只在调用期间借用 key 中的视图。insert 必须深拷贝 function key 与
 * logical program counter 字节；lease.snapshot 的视图指向 repository
 * 拥有的不可变副本，并只在 lease 释放前有效。
 */

typedef enum runtimec_compilation_repository_lookup_result_t {
    RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_INVALID = 0,
    RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_FOUND = 1,
    RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND = 2
} runtimec_compilation_repository_lookup_result_t;

typedef struct runtimec_compilation_repository_lease_t {
    const binrwir_compilation_t *compilation;
    runtimec_snapshot_t snapshot;
    void *token;
} runtimec_compilation_repository_lease_t;

runtimec_status_t runtimec_compilation_repository_create(
    runtimec_allocator_t allocator,
    size_t capacity,
    runtimec_compilation_repository_t **output_repository,
    runtimec_error_t *error);

runtimec_status_t runtimec_compilation_repository_destroy(
    runtimec_compilation_repository_t *repository);

runtimec_status_t runtimec_compilation_repository_find(
    runtimec_compilation_repository_t *repository,
    const runtimec_compilation_repository_key_t *key,
    runtimec_compilation_repository_lookup_result_t *output_lookup_result,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_error_t *error);

runtimec_status_t runtimec_compilation_repository_insert(
    runtimec_compilation_repository_t *repository,
    const runtimec_compilation_repository_key_t *key,
    binrwir_compilation_t *compilation,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_error_t *error);

runtimec_status_t runtimec_compilation_repository_release(
    runtimec_compilation_repository_t *repository,
    runtimec_compilation_repository_lease_t *lease);

runtimec_status_t runtimec_compilation_repository_invalidate(
    runtimec_compilation_repository_t *repository,
    const runtimec_snapshot_t *snapshot);

runtimec_status_t runtimec_compilation_repository_clear(
    runtimec_compilation_repository_t *repository);

#ifdef __cplusplus
}
#endif

#endif
