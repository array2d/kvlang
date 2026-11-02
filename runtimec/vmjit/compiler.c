/* vmjit/compiler 的失败封闭骨架。 */

#include "vmjit/compiler.h"

static void vmjit_compilation_result_reset(
    vmjit_compilation_result_t *result,
    vmjit_compilation_outcome_t outcome,
    vmjit_refusal_reason_t refusal_reason) {
    if (result != NULL) {
        *result = (vmjit_compilation_result_t){0};
        result->outcome = outcome;
        result->refusal.reason = refusal_reason;
    }
}

runtimec_status_t vmjit_compiler_create(
    runtimec_allocator_t allocator,
    const vmjit_compiler_configuration_t *configuration,
    vmjit_compiler_t **output_compiler,
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

runtimec_status_t vmjit_compiler_destroy(vmjit_compiler_t *compiler) {
    if (compiler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_compile(
    vmjit_compiler_t *compiler,
    const binrwir_program_t *program,
    const binrwir_execution_plan_t *execution_plan,
    vmjit_compilation_result_t *output_result,
    runtimec_error_t *error) {
    (void)error;
    vmjit_compilation_result_reset(
        output_result,
        VMJIT_COMPILATION_OUTCOME_INVALID,
        VMJIT_REFUSAL_INVALID_PROGRAM);
    if (compiler == NULL || program == NULL || execution_plan == NULL ||
        output_result == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    vmjit_compilation_result_reset(
        output_result,
        VMJIT_COMPILATION_OUTCOME_ERROR,
        VMJIT_REFUSAL_NONE);
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
