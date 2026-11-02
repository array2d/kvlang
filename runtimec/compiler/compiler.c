/*
 * BinRWIR Compiler 当前只提供失败封闭桩。
 * 输出先进入无效状态；合法但尚未实现的请求返回 NOT_IMPLEMENTED。
 */

#include "compiler/compiler.h"

static void binrwir_compilation_reset_view(
    binrwir_compilation_view_t *view) {
    if (view != NULL) {
        *view = (binrwir_compilation_view_t){
            .execution_target = BINRWIR_EXECUTION_TARGET_INVALID,
            .canonical_program = NULL,
            .execution_plan = NULL,
            .vmjit_code = NULL,
        };
    }
}

runtimec_status_t binrwir_compiler_create(
    const runtimec_allocator_t *allocator,
    const binrwir_compiler_configuration_t *configuration,
    binrwir_compiler_t **output_compiler,
    runtimec_error_t *error) {
    if (output_compiler != NULL) {
        *output_compiler = NULL;
    }
    if (!runtimec_allocator_is_valid(allocator) || configuration == NULL ||
        output_compiler == NULL ||
        configuration->structure_size != sizeof(*configuration) ||
        configuration->compiler_revision == 0U ||
        (configuration->enable_vmjit &&
         configuration->vmjit_compiler == NULL) ||
        (!configuration->enable_vmjit &&
         configuration->vmjit_compiler != NULL)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_compiler_destroy(
    binrwir_compiler_t *compiler,
    runtimec_error_t *error) {
    if (compiler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_compiler_get_revision(
    const binrwir_compiler_t *compiler,
    uint32_t *output_compiler_revision) {
    if (output_compiler_revision != NULL) {
        *output_compiler_revision = UINT32_MAX;
    }
    if (compiler == NULL || output_compiler_revision == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_compiler_compile(
    binrwir_compiler_t *compiler,
    const binrwir_program_t *canonical_program,
    binrwir_compilation_t **output_compilation,
    runtimec_error_t *error) {
    if (output_compilation != NULL) {
        *output_compilation = NULL;
    }
    if (compiler == NULL || canonical_program == NULL ||
        output_compilation == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_compilation_get_view(
    const binrwir_compilation_t *compilation,
    binrwir_compilation_view_t *output_view,
    runtimec_error_t *error) {
    binrwir_compilation_reset_view(output_view);
    if (compilation == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_compilation_destroy(
    binrwir_compilation_t *compilation,
    runtimec_error_t *error) {
    if (compilation == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
