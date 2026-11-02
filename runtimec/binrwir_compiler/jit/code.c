/* binrwir_compiler/jit/code 的失败封闭骨架。 */

#include "binrwir_compiler/jit/code.h"

runtimec_status_t binrwir_jit_code_create(
    binrwir_jit_code_memory_t *memory,
    size_t machine_code_size,
    size_t entry_byte_offset,
    binrwir_jit_code_t **output_code,
    runtimec_error_t *error) {
    (void)error;
    if (output_code != NULL) {
        *output_code = NULL;
    }
    if (memory == NULL || machine_code_size == 0U ||
        entry_byte_offset >= machine_code_size || output_code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_code_retain(binrwir_jit_code_t *code) {
    if (code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_code_release(binrwir_jit_code_t *code) {
    if (code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_code_get_machine_code_function(
    const binrwir_jit_code_t *code,
    binrwir_jit_machine_code_function_t *output_function) {
    if (output_function != NULL) {
        *output_function = NULL;
    }
    if (code == NULL || output_function == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
