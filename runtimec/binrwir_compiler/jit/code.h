#ifndef RUNTIMEC_BINRWIR_JIT_CODE_H
#define RUNTIMEC_BINRWIR_JIT_CODE_H

#include <stddef.h>

#include "contract/snapshot.h"
#include "binrwir_compiler/jit/backend.h"
#include "binrwir_compiler/jit/code_memory.h"
#include "binrwir_compiler/jit/execution_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_jit_code binrwir_jit_code_t;

runtimec_status_t binrwir_jit_code_create(
    binrwir_jit_code_memory_t *memory,
    size_t machine_code_size,
    size_t entry_byte_offset,
    binrwir_jit_code_t **output_code,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_code_retain(binrwir_jit_code_t *code);
runtimec_status_t binrwir_jit_code_release(binrwir_jit_code_t *code);
runtimec_status_t binrwir_jit_code_get_machine_code_function(
    const binrwir_jit_code_t *code,
    binrwir_jit_machine_code_function_t *output_function);

#ifdef __cplusplus
}
#endif

#endif
