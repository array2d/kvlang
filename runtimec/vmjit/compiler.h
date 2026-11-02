#ifndef RUNTIMEC_VMJIT_COMPILER_H
#define RUNTIMEC_VMJIT_COMPILER_H

/*
 * 组件：vmjit/compiler
 * 作用：vmjit 机器码编译入口：消费 canonical Program 与顶层 BinRWIR
 *       Compiler 已生成的 execution plan，完成验证、lowering 与 backend 生成。
 * 联系：顶层 BinRWIR Compiler 在编译阶段调用；本组件先验证 canonical
 *       BinRWIR 与 execution plan，再生成机器码并返回编译判定。
 * 约束：compile 不读 RWIR/kvspace；只有 COMPILED 携带 code。内部阶段的
 *       UNSUPPORTED 转成 SUCCESS + NOT_COMPILABLE；非法 BinRWIR、内存不足
 *       和内部错误返回原错误状态 + ERROR，不能建立可执行 code。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "binrwir/execution_plan.h"
#include "binrwir/program.h"
#include "vmjit/backend.h"
#include "vmjit/code.h"
#include "vmjit/verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmjit_compiler_t vmjit_compiler_t;
typedef enum vmjit_compilation_outcome_t {
    VMJIT_COMPILATION_OUTCOME_INVALID = 0,
    VMJIT_COMPILATION_OUTCOME_COMPILED = 1,
    VMJIT_COMPILATION_OUTCOME_NOT_COMPILABLE = 2,
    VMJIT_COMPILATION_OUTCOME_ERROR = 3
} vmjit_compilation_outcome_t;
typedef struct vmjit_compilation_result_t {
    vmjit_compilation_outcome_t outcome;
    /* 仅 COMPILED 可非 NULL；其它 outcome 必须保持 NULL。 */
    vmjit_code_t *code;
    vmjit_refusal_t refusal;
} vmjit_compilation_result_t;
typedef struct vmjit_compiler_configuration_t {
    uint32_t structure_size;
    size_t machine_code_capacity;
    uint64_t generation;
    vmjit_target_t target;
} vmjit_compiler_configuration_t;

/* create 必须复制 target.triple 字节；configuration 只在调用期间有效。 */

runtimec_status_t vmjit_compiler_create(
    runtimec_allocator_t allocator,
    const vmjit_compiler_configuration_t *configuration,
    vmjit_compiler_t **output_compiler,
    runtimec_error_t *error);
runtimec_status_t vmjit_compiler_destroy(vmjit_compiler_t *compiler);
runtimec_status_t vmjit_compile(
    vmjit_compiler_t *compiler,
    const binrwir_program_t *program,
    const binrwir_execution_plan_t *execution_plan,
    vmjit_compilation_result_t *output_result,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
