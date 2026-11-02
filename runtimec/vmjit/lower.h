#ifndef RUNTIMEC_VMJIT_LOWER_H
#define RUNTIMEC_VMJIT_LOWER_H

/*
 * 组件：vmjit/lower
 * 作用：把已验证的 plan 转成 target-neutral micro operation，不重新解释 RWIR
 *       语义。
 * 联系：plan 来自 vmjit/plan；输出交给 backend 的 emit；lower 不接触
 *       kvspace/host。
 * 约束：具体 micro operation 编码当前未冻结。
 */

#include "contract/allocator.h"
#include "contract/status.h"
#include "vmjit/plan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmjit_lowered_program vmjit_lowered_program_t;
runtimec_status_t vmjit_lower(
    const vmjit_plan_t *plan,
    const runtimec_allocator_t *allocator,
    vmjit_lowered_program_t **output_program,
    runtimec_error_t *error);
runtimec_status_t vmjit_lowered_program_destroy(
    vmjit_lowered_program_t *program);

#ifdef __cplusplus
}
#endif

#endif
