#ifndef RUNTIMEC_COMPILER_OPTIMIZATION_H
#define RUNTIMEC_COMPILER_OPTIMIZATION_H

/*
 * 组件：BinRWIR Compiler optimization
 * 作用：从 canonical BinRWIR Program 生成只读 execution plan，统一承载
 *       指令编码、类型固化、KV 寻址、指令融合、虚拟寄存器与控制流优化。
 * 联系：仅由 compiler/compiler.c 调用；vmjit 与 vmbin 只消费完成的 plan。
 * 约束：不得改变 Program 语义、Read/Write authority、alias 顺序或 source
 *       logical program counter 观察边界，不得选择或执行目标。
 */

#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "binrwir/program.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t binrwir_compiler_build_execution_plan(
    const runtimec_allocator_t *allocator,
    const binrwir_program_t *canonical_program,
    uint64_t plan_generation,
    uint32_t compiler_revision,
    binrwir_execution_plan_t **output_execution_plan,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
