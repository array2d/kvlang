#ifndef RUNTIMEC_BINRWIR_JIT_LOWER_H
#define RUNTIMEC_BINRWIR_JIT_LOWER_H

/*
 * 组件：binrwir_compiler/jit/lower
 * 作用：把目标相关 lowering plan 转成机器码生成请求，保留每个 register/spill
 *       到 V.body 的来源映射、逐 logical instruction commit safepoint，以及
 *       guard/error discard 路径，不重新解释 RWIR 语义。
 * 联系：plan 来自 binrwir_compiler/jit/lowering_plan；输出交给 backend 的 emit；lower 不接触
 *       kvspace/host。
 * 约束：不得删除 instruction completion boundary，不得将 speculative/partial
 *       state 放入 commit 表，也不得产生无法恢复来源的临时值；具体 micro
 *       operation 编码当前未冻结。
 */

#include "contract/allocator.h"
#include "contract/status.h"
#include "binrwir_compiler/jit/lowering_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_jit_lowered_program binrwir_jit_lowered_program_t;
runtimec_status_t binrwir_jit_lower(
    const binrwir_jit_lowering_plan_t *plan,
    const runtimec_allocator_t *allocator,
    binrwir_jit_lowered_program_t **output_program,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_lowered_program_destroy(
    binrwir_jit_lowered_program_t *program);

#ifdef __cplusplus
}
#endif

#endif
