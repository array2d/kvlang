/* vmjit/code 的失败封闭骨架。 */

#include "vmjit/code.h"

runtimec_status_t vmjit_code_create(
    vmjit_code_memory_t *memory,
    size_t machine_code_size,
    size_t entry_byte_offset,
    vmjit_code_t **output_code,
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

runtimec_status_t vmjit_code_retain(vmjit_code_t *code) {
    if (code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_release(vmjit_code_t *code) {
    if (code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_get_machine_code_function(
    const vmjit_code_t *code,
    vmjit_machine_code_function_t *output_function) {
    if (output_function != NULL) {
        *output_function = NULL;
    }
    if (code == NULL || output_function == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
