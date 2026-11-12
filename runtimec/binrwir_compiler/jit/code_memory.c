
#include "binrwir_compiler/jit/code_memory.h"

runtimec_status_t binrwir_jit_code_memory_create(
    size_t capacity,
    binrwir_jit_code_memory_t **output_memory,
    runtimec_error_t *error) {
    (void)error;
    if (output_memory != NULL) {
        *output_memory = NULL;
    }
    if (capacity == 0U || output_memory == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_code_memory_writable_view(
    binrwir_jit_code_memory_t *memory,
    runtimec_bytes_buffer_t *output_buffer,
    runtimec_error_t *error) {
    (void)error;
    if (output_buffer != NULL) {
        runtimec_bytes_buffer_reset(output_buffer);
    }
    if (memory == NULL || output_buffer == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_code_memory_publish(
    binrwir_jit_code_memory_t *memory,
    size_t code_size,
    runtimec_error_t *error) {
    (void)error;
    if (memory == NULL || code_size == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_jit_code_memory_destroy(binrwir_jit_code_memory_t *memory) {
    if (memory == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
