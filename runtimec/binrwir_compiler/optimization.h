#ifndef RUNTIMEC_BINRWIR_COMPILER_OPTIMIZATION_H
#define RUNTIMEC_BINRWIR_COMPILER_OPTIMIZATION_H

/*
 * 组件：BinRWIR Compiler optimization
 * 作用：从已经地址化的 canonical BinRWIR Program 生成只读 execution plan，
 *       只承载类型固化、dispatch 融合、控制流与 source logical program
 *       counter 映射。
 * 联系：仅由 binrwir_compiler/compiler.c 调用；binrwir_jit 与 vmbin 只消费完成的 plan。
 * 约束：不得改变 Program 语义、Read/Write authority、alias 顺序或 source
 *       logical program counter 观察边界，不得选择或执行目标。不得重复编码
 *       instruction、解析或缓存 V.body 地址，也不得建立中间值存储模型。
 */

#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "binrwir/binfunc.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t binrwir_compiler_build_execution_plan(
    const runtimec_allocator_t *allocator,
    const binrwir_binfunc_t *canonical_program,
    uint64_t plan_generation,
    uint32_t compiler_revision,
    binrwir_execution_plan_t **output_execution_plan,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
