/*
 * 组件：vmkv 未实现入口
 * 作用：先把输出置为明确无效，再验证接口形状，最后返回
 *       RUNTIMEC_STATUS_NOT_IMPLEMENTED。
 * 约束：不产生空 Program 伪装成功，不执行 RWIR，不生成机器码，不走兼容路径。
 */

#include "vmkv/compiler.h"
#include "vmkv/rwir.h"
#include "vmkv/source.h"

runtimec_status_t vmkv_source_open_function(
    const runtimec_host_services_t *host_services,
    runtimec_string_view_t function_path,
    vmkv_function_source_t **output_function,
    runtimec_snapshot_t *output_snapshot,
    runtimec_error_t *error) {
    if (output_function != NULL) {
        *output_function = NULL;
    }
    if (output_snapshot != NULL) {
        runtimec_snapshot_reset(output_snapshot);
    }
    if (!runtimec_host_services_is_valid(host_services) ||
        !runtimec_string_view_is_valid(function_path) ||
        output_function == NULL || output_snapshot == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmkv_source_close_function(vmkv_function_source_t *function) {
    if (function == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmkv_rwir_reader_create(
    vmkv_function_source_t *function,
    vmkv_rwir_reader_t **output_reader,
    runtimec_error_t *error) {
    if (output_reader != NULL) {
        *output_reader = NULL;
    }
    if (function == NULL || output_reader == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmkv_rwir_reader_next(
    vmkv_rwir_reader_t *reader,
    vmkv_rwir_read_result_t *output_result,
    vmkv_rwir_instruction_view_t *output_instruction,
    runtimec_error_t *error) {
    if (output_result != NULL) {
        *output_result = VMKV_RWIR_READ_RESULT_INVALID;
    }
    if (output_instruction != NULL) {
        *output_instruction = (vmkv_rwir_instruction_view_t){0};
    }
    if (reader == NULL || output_result == NULL || output_instruction == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmkv_rwir_reader_destroy(vmkv_rwir_reader_t *reader) {
    if (reader == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmkv_compiler_create(
    runtimec_allocator_t allocator,
    vmkv_compiler_t **output_compiler,
    runtimec_error_t *error) {
    if (output_compiler != NULL) {
        *output_compiler = NULL;
    }
    if (!runtimec_allocator_is_valid(&allocator) || output_compiler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmkv_compiler_destroy(vmkv_compiler_t *compiler) {
    if (compiler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmkv_compile(
    vmkv_compiler_t *compiler,
    const vmkv_compile_request_t *request,
    vmbin_program_t **output_program,
    runtimec_error_t *error) {
    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (compiler == NULL || request == NULL || output_program == NULL ||
        !runtimec_host_services_is_valid(request->host_services) ||
        !runtimec_string_view_is_valid(request->function_path) ||
        !runtimec_snapshot_is_valid(&request->expected_snapshot)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)error;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
