#ifndef RUNTIMEC_BINRWIR_COMPILER_OPTIMIZATION_H
#define RUNTIMEC_BINRWIR_COMPILER_OPTIMIZATION_H

#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "binrwir/binfunc.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t binrwir_compiler_build_execution_plan(
    const runtimec_allocator_t *allocator,
    const binrwir_binfunc_t *canonical_program,
    uint64_t plan_generation,
    uint32_t compiler_revision,
    binrwir_execution_plan_t **output_execution_plan,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
