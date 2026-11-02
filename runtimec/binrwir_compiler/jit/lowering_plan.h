#ifndef RUNTIMEC_BINRWIR_JIT_LOWERING_PLAN_H
#define RUNTIMEC_BINRWIR_JIT_LOWERING_PLAN_H

/*
 * 组件：binrwir_compiler/jit/lowering_plan
 * 作用：把已验证 Program 与顶层 Compiler 的 execution plan 绑定为目标相关
 *       lowering 输入，包括调用约定、物理寄存器、spill、常量物化、safepoint
 *       和 source logical program counter 映射。
 * 联系：binrwir_jit compiler 构建，lower 消费；backend 只消费 emit request。
 * 约束：不得重新优化 BinRWIR、改变 execution target 或建立执行期替代路径；
 *       无法精确表达 execution plan 时必须在机器码生成前返回
 *       RUNTIMEC_STATUS_UNSUPPORTED 并填写 refusal。顶层 binrwir_jit_compile 只把
 *       UNSUPPORTED 映射为成功的 NOT_COMPILABLE，其它错误原样停止。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "contract/allocator.h"
#include "contract/status.h"
#include "binrwir_compiler/jit/backend.h"
#include "binrwir_compiler/jit/verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_jit_lowering_plan binrwir_jit_lowering_plan_t;

typedef struct binrwir_jit_lowering_plan_options_t {
    size_t structure_size;
    uint32_t code_generation_revision;
    bool observe_every_instruction;
} binrwir_jit_lowering_plan_options_t;

runtimec_status_t binrwir_jit_lowering_plan_create(
    const binrwir_jit_verified_program_t *verified_program,
    const binrwir_execution_plan_t *execution_plan,
    const binrwir_jit_target_t *target,
    const binrwir_jit_lowering_plan_options_t *options,
    const runtimec_allocator_t *allocator,
    binrwir_jit_lowering_plan_t **output_plan,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error);

runtimec_status_t binrwir_jit_lowering_plan_destroy(binrwir_jit_lowering_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif
