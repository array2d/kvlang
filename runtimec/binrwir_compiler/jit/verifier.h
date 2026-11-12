#ifndef RUNTIMEC_BINRWIR_JIT_VERIFIER_H
#define RUNTIMEC_BINRWIR_JIT_VERIFIER_H

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/binfunc.h"
#include "binrwir_compiler/jit/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_jit_refusal_reason_t {
    BINRWIR_JIT_REFUSAL_NONE = 0,
    BINRWIR_JIT_REFUSAL_DISABLED = 1,
    BINRWIR_JIT_REFUSAL_UNSUPPORTED_TARGET = 2,
    BINRWIR_JIT_REFUSAL_UNSUPPORTED_PROGRAM = 3,
    BINRWIR_JIT_REFUSAL_INVALID_PROGRAM = 4,
    BINRWIR_JIT_REFUSAL_RESOURCE_LIMIT = 5
} binrwir_jit_refusal_reason_t;
typedef struct binrwir_jit_refusal_t {
    binrwir_jit_refusal_reason_t reason;
    uint32_t logical_program_counter;
    uint32_t detail_code;
} binrwir_jit_refusal_t;

typedef struct binrwir_jit_verified_program_t {
    const binrwir_binfunc_t *program;
} binrwir_jit_verified_program_t;

runtimec_status_t binrwir_jit_verify_program(
    const binrwir_binfunc_t *program,
    const binrwir_jit_target_t *target,
    binrwir_jit_verified_program_t *output_verified_program,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
