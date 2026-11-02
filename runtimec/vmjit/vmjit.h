#ifndef RUNTIMEC_VMJIT_H
#define RUNTIMEC_VMJIT_H

/*
 * 组件：vmjit/vmjit
 * 作用：vmjit 总入口与执行结果契约：EXECUTED / FALLBACK / ERROR。
 *       unsupported target、operation、disabled、negative
 *       cache、guard 或 deoptimization 应返回 FALLBACK；任何 side exit 必须落在
 *       source-mapped、未重复副作用的安全边界。INVALID、NOT_IMPLEMENTED、
 *       out-of-memory 或 ERROR 是错误，不得伪装成 FALLBACK。
 * 联系：engine/route 是唯一调用者；输入是只读 vmbin Program + Frame；
 *       输出为结构化 exit，交给 route 决定后续提交或回退。
 * 约束：vmjit 不读 RWIR、不访问 kvspace、不拥有持久 program counter 或 status；
 *       try_execute 只接受 verifier 产生的 verified Program。
 */

#include "contract/status.h"
#include "vmbin/exit.h"
#include "vmbin/frame.h"
#include "vmjit/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmjit_execution_outcome_t {
    VMJIT_EXECUTION_OUTCOME_INVALID = 0,
    VMJIT_EXECUTION_OUTCOME_EXECUTED = 1,
    VMJIT_EXECUTION_OUTCOME_FALLBACK = 2,
    VMJIT_EXECUTION_OUTCOME_ERROR = 3
} vmjit_execution_outcome_t;
typedef struct vmjit_execution_result_t {
    vmjit_execution_outcome_t outcome;
    runtimec_status_t status;
    vmbin_exit_t exit;
} vmjit_execution_result_t;
vmjit_execution_result_t vmjit_try_execute(
    vmjit_compiler_t *compiler,
    vmjit_code_cache_t *cache,
    const vmjit_verified_program_t *verified_program,
    vmbin_frame_t *frame,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
