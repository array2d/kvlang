#ifndef RUNTIMEC_BINRWIR_JIT_CODE_MEMORY_H
#define RUNTIMEC_BINRWIR_JIT_CODE_MEMORY_H

#include <stddef.h>

#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_jit_code_memory binrwir_jit_code_memory_t;
runtimec_status_t binrwir_jit_code_memory_create(
    size_t capacity,
    binrwir_jit_code_memory_t **output_memory,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_code_memory_writable_view(
    binrwir_jit_code_memory_t *memory,
    runtimec_bytes_buffer_t *output_buffer,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_code_memory_publish(
    binrwir_jit_code_memory_t *memory,
    size_t code_size,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_code_memory_destroy(binrwir_jit_code_memory_t *memory);

#ifdef __cplusplus
}
#endif

#endif
