#ifndef RUNTIMEC_BINRWIR_JIT_COMPILER_H
#define RUNTIMEC_BINRWIR_JIT_COMPILER_H

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
    
    binrwir_jit_code_t *code;
    binrwir_jit_refusal_t refusal;
} binrwir_jit_compilation_result_t;
typedef struct binrwir_jit_compiler_configuration_t {
    uint32_t structure_size;
    size_t machine_code_capacity;
    uint64_t generation;
    binrwir_jit_target_t target;
} binrwir_jit_compiler_configuration_t;

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
