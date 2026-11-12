#ifndef RUNTIMEC_BINRWIR_COMPILER_COMPILER_H
#define RUNTIMEC_BINRWIR_COMPILER_COMPILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "binrwir/binfunc.h"
#include "binrwir_compiler/jit/backend.h"
#include "binrwir_compiler/jit/code.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_compiler_t binrwir_compiler_t;
typedef struct binrwir_compilation_t binrwir_compilation_t;

typedef enum binrwir_execution_target_t {
    BINRWIR_EXECUTION_TARGET_INVALID = 0,
    BINRWIR_EXECUTION_TARGET_VMBIN = 1,
    BINRWIR_EXECUTION_TARGET_JIT = 2
} binrwir_execution_target_t;

typedef struct binrwir_compiler_configuration_t {
    size_t structure_size;
    uint32_t compiler_revision;
    bool enable_jit;
    size_t jit_machine_code_capacity;
    binrwir_jit_target_t jit_target;
} binrwir_compiler_configuration_t;

typedef struct binrwir_compilation_view_t {
    binrwir_execution_target_t execution_target;
    const binrwir_binfunc_t *canonical_program;
    const binrwir_execution_plan_t *execution_plan;
    const binrwir_jit_code_t *binrwir_jit_code;
} binrwir_compilation_view_t;

runtimec_status_t binrwir_compiler_create(
    const runtimec_allocator_t *allocator,
    const binrwir_compiler_configuration_t *configuration,
    binrwir_compiler_t **output_compiler,
    runtimec_error_t *error);

runtimec_status_t binrwir_compiler_destroy(
    binrwir_compiler_t *compiler,
    runtimec_error_t *error);

runtimec_status_t binrwir_compiler_get_revision(
    const binrwir_compiler_t *compiler,
    uint32_t *output_compiler_revision);

runtimec_status_t binrwir_compiler_compile(
    binrwir_compiler_t *compiler, binrwir_binfunc_t *program,
    binrwir_compilation_t **output_compilation, runtimec_error_t *error);

runtimec_status_t binrwir_compilation_get_view(
    const binrwir_compilation_t *compilation,
    binrwir_compilation_view_t *output_view,
    runtimec_error_t *error);

runtimec_status_t binrwir_compilation_destroy(
    binrwir_compilation_t *compilation,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
