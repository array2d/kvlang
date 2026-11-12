#ifndef RUNTIMEC_BINRWIR_JIT_LOWER_H
#define RUNTIMEC_BINRWIR_JIT_LOWER_H

#include "contract/allocator.h"
#include "contract/status.h"
#include "binrwir_compiler/jit/lowering_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_jit_lowered_program binrwir_jit_lowered_program_t;
runtimec_status_t binrwir_jit_lower(
    const binrwir_jit_lowering_plan_t *plan,
    const runtimec_allocator_t *allocator,
    binrwir_jit_lowered_program_t **output_program,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_lowered_program_destroy(
    binrwir_jit_lowered_program_t *program);

#ifdef __cplusplus
}
#endif

#endif
