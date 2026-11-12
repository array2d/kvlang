#ifndef RUNTIMEC_BINRWIR_JIT_BACKEND_H
#define RUNTIMEC_BINRWIR_JIT_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_jit_architecture_t {
    BINRWIR_JIT_ARCHITECTURE_NONE = 0,
    BINRWIR_JIT_ARCHITECTURE_X86_64 = 1,
    BINRWIR_JIT_ARCHITECTURE_AARCH64 = 2
} binrwir_jit_architecture_t;
typedef struct binrwir_jit_target_t {
    binrwir_jit_architecture_t architecture;
    runtimec_string_view_t triple;
    uint64_t feature_mask;
} binrwir_jit_target_t;

struct binrwir_jit_lowered_program;
struct binrwir_jit_register_allocation;
struct binrwir_jit_safepoint_plan;

typedef struct binrwir_jit_emit_request_t {
    const struct binrwir_jit_lowered_program *lowered_program;
    const struct binrwir_jit_register_allocation *register_allocation;
    const struct binrwir_jit_safepoint_plan *safepoint_plan;
    runtimec_bytes_buffer_t writable_code;
} binrwir_jit_emit_request_t;

typedef struct binrwir_jit_emit_result_t {
    size_t machine_code_size;
    size_t entry_byte_offset;
} binrwir_jit_emit_result_t;
typedef struct binrwir_jit_backend_t {
    binrwir_jit_architecture_t architecture;
    runtimec_string_view_t name;
    bool (*supports)(const binrwir_jit_target_t *target);
    runtimec_status_t (*emit)(
        const binrwir_jit_emit_request_t *request,
        binrwir_jit_emit_result_t *output_result,
        runtimec_error_t *error);
} binrwir_jit_backend_t;
runtimec_status_t binrwir_jit_backend_for_target(
    const binrwir_jit_target_t *target,
    const binrwir_jit_backend_t **output_backend);
runtimec_status_t binrwir_jit_x86_64_backend(
    const binrwir_jit_backend_t **output_backend);
runtimec_status_t binrwir_jit_aarch64_backend(
    const binrwir_jit_backend_t **output_backend);

#ifdef __cplusplus
}
#endif

#endif
