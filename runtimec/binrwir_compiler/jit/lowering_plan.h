#ifndef RUNTIMEC_BINRWIR_JIT_LOWERING_PLAN_H
#define RUNTIMEC_BINRWIR_JIT_LOWERING_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "contract/allocator.h"
#include "contract/status.h"
#include "binrwir_compiler/jit/backend.h"
#include "binrwir_compiler/jit/register_allocation.h"
#include "binrwir_compiler/jit/safepoint.h"
#include "binrwir_compiler/jit/verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_jit_lowering_plan binrwir_jit_lowering_plan_t;

typedef struct binrwir_jit_lowering_plan_options_t {
    size_t structure_size;
    uint32_t code_generation_revision;
} binrwir_jit_lowering_plan_options_t;

runtimec_status_t binrwir_jit_lowering_plan_create(
    const binrwir_jit_verified_program_t *verified_program,
    const binrwir_execution_plan_t *execution_plan,
    const binrwir_jit_target_t *target,
    const binrwir_jit_lowering_plan_options_t *options,
    const runtimec_allocator_t *allocator,
    binrwir_jit_lowering_plan_t **output_plan,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error);

runtimec_status_t binrwir_jit_lowering_plan_destroy(binrwir_jit_lowering_plan_t *plan);
runtimec_status_t binrwir_jit_lowering_plan_get_register_allocation(
    const binrwir_jit_lowering_plan_t *plan,
    const binrwir_jit_register_allocation_t **output_allocation);
runtimec_status_t binrwir_jit_lowering_plan_get_safepoint_plan(
    const binrwir_jit_lowering_plan_t *plan,
    const binrwir_jit_safepoint_plan_t **output_safepoint_plan);

#ifdef __cplusplus
}
#endif

#endif
