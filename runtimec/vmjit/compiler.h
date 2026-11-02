#ifndef RUNTIMEC_VMJIT_COMPILER_H
#define RUNTIMEC_VMJIT_COMPILER_H

/*
 * 组件：vmjit/compiler
 * 作用：单一 vmjit pipeline 的编排入口：configuration、target、code cache
 *       limit、generation 与 compile（Program -> code）。所有优化层级共享
 *       同一 planner、binary interface、cache 与路由。
 * 联系：engine/route 在 vmjit enabled 且 eligible 时调用；输入是 verifier
 *       产生的只读 verified Program；输出 code 交给 cache。
 * 约束：compile 不读 RWIR/kvspace；无法证明安全时拒绝，不“尽量执行”。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "vmbin/program.h"
#include "vmjit/backend.h"
#include "vmjit/code.h"
#include "vmjit/verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmjit_compiler_t vmjit_compiler_t;
typedef struct vmjit_config_t {
    uint32_t structure_size;
    bool enabled;
    size_t code_cache_limit;
    uint64_t generation;
    vmjit_target_t target;
} vmjit_config_t;

runtimec_status_t vmjit_compiler_create(
    runtimec_allocator_t allocator,
    const vmjit_config_t *config,
    vmjit_compiler_t **output_compiler,
    runtimec_error_t *error);
runtimec_status_t vmjit_compiler_destroy(vmjit_compiler_t *compiler);
runtimec_status_t vmjit_compile(
    vmjit_compiler_t *compiler,
    const vmjit_verified_program_t *verified_program,
    vmjit_code_t **output_code,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
