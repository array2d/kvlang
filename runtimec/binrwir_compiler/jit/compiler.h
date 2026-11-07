#ifndef RUNTIMEC_BINRWIR_JIT_COMPILER_H
#define RUNTIMEC_BINRWIR_JIT_COMPILER_H

/*
 * 组件：binrwir_compiler/jit/compiler
 * 作用：binrwir_jit 机器码编译入口：消费 canonical Program 与顶层 BinRWIR
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
#include "binrwir/binfunc.h"
#include "binrwir_compiler/jit/backend.h"
#include "binrwir_compiler/jit/code.h"
#include "binrwir_compiler/jit/verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_jit_compiler_t binrwir_jit_compiler_t;
typedef enum binrwir_jit_compilation_outcome_t {
    BINRWIR_JIT_COMPILATION_OUTCOME_INVALID = 0,
    BINRWIR_JIT_COMPILATION_OUTCOME_COMPILED = 1,
    BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE = 2,
    BINRWIR_JIT_COMPILATION_OUTCOME_ERROR = 3
} binrwir_jit_compilation_outcome_t;
typedef struct binrwir_jit_compilation_result_t {
    binrwir_jit_compilation_outcome_t outcome;
    /* 仅 COMPILED 可非 NULL；其它 outcome 必须保持 NULL。 */
    binrwir_jit_code_t *code;
    binrwir_jit_refusal_t refusal;
} binrwir_jit_compilation_result_t;
typedef struct binrwir_jit_compiler_configuration_t {
    uint32_t structure_size;
    size_t machine_code_capacity;
    uint64_t generation;
    binrwir_jit_target_t target;
} binrwir_jit_compiler_configuration_t;

/* create 必须复制 target.triple 字节；configuration 只在调用期间有效。 */

runtimec_status_t binrwir_jit_compiler_create(
    runtimec_allocator_t allocator,
    const binrwir_jit_compiler_configuration_t *configuration,
    binrwir_jit_compiler_t **output_compiler,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_compiler_destroy(binrwir_jit_compiler_t *compiler);
runtimec_status_t binrwir_jit_compile(
    binrwir_jit_compiler_t *compiler,
    const binrwir_binfunc_t *program,
    const binrwir_execution_plan_t *execution_plan,
    binrwir_jit_compilation_result_t *output_result,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
