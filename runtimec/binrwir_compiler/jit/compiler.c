/* binrwir_compiler/jit/compiler 的失败封闭骨架。 */

#include "binrwir_compiler/jit/compiler.h"

static void binrwir_jit_compilation_result_reset(
    binrwir_jit_compilation_result_t *result,
    binrwir_jit_compilation_outcome_t outcome,
    binrwir_jit_refusal_reason_t refusal_reason) {
    if (result != NULL) {
        *result = (binrwir_jit_compilation_result_t){0};
        result->outcome = outcome;
        result->refusal.reason = refusal_reason;
    }
}

runtimec_status_t binrwir_jit_compiler_create(
    runtimec_allocator_t allocator,
    const binrwir_jit_compiler_configuration_t *configuration,
    binrwir_jit_compiler_t **output_compiler,
    runtimec_error_t *error) {
    (void)error;
    if (output_compiler != NULL) {
        *output_compiler = NULL;
    }
    if (!runtimec_allocator_is_valid(&allocator) || configuration == NULL ||
        configuration->structure_size != sizeof(*configuration) ||
        configuration->machine_code_capacity == 0U ||
        output_compiler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_compiler_destroy(binrwir_jit_compiler_t *compiler) {
    if (compiler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_compile(
    binrwir_jit_compiler_t *compiler,
    const binrwir_binfunc_t *program,
    const binrwir_execution_plan_t *execution_plan,
    binrwir_jit_compilation_result_t *output_result,
    runtimec_error_t *error) {
    (void)error;
    binrwir_jit_compilation_result_reset(
        output_result,
        BINRWIR_JIT_COMPILATION_OUTCOME_INVALID,
        BINRWIR_JIT_REFUSAL_INVALID_PROGRAM);
    if (compiler == NULL || program == NULL || execution_plan == NULL ||
        output_result == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    binrwir_jit_compilation_result_reset(
        output_result,
        BINRWIR_JIT_COMPILATION_OUTCOME_ERROR,
        BINRWIR_JIT_REFUSAL_NONE);
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
