
#include "binrwir_compiler/compiler.h"

#include <string.h>

#include "binrwir_compiler/optimization.h"

struct binrwir_compiler_t {
    runtimec_allocator_t allocator;
    uint32_t compiler_revision;
    
    uint64_t plan_generation;
};

struct binrwir_compilation_t {
    runtimec_allocator_t allocator;
    binrwir_compilation_view_t view;
    
    binrwir_binfunc_t *canonical_program;
    binrwir_execution_plan_t *execution_plan;
};

static runtimec_status_t binrwir_compiler_fail(runtimec_error_t *error,
                                               runtimec_status_t status,
                                               int64_t code,
                                               const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static void binrwir_compilation_reset_view(binrwir_compilation_view_t *view) {
    if (view != NULL) {
        *view = (binrwir_compilation_view_t){
            .execution_target = BINRWIR_EXECUTION_TARGET_INVALID,
            .canonical_program = NULL,
            .execution_plan = NULL,
            .binrwir_jit_code = NULL,
        };
    }
}

static bool binrwir_compiler_jit_configuration_is_valid(
    const binrwir_compiler_configuration_t *configuration) {
    if (configuration->enable_jit) {
        return configuration->jit_machine_code_capacity > 0U &&
               configuration->jit_target.architecture !=
                   BINRWIR_JIT_ARCHITECTURE_NONE &&
               runtimec_string_view_is_valid(configuration->jit_target.triple);
    }
    return configuration->jit_machine_code_capacity == 0U &&
           configuration->jit_target.architecture ==
               BINRWIR_JIT_ARCHITECTURE_NONE &&
           configuration->jit_target.triple.data == NULL &&
           configuration->jit_target.triple.size == 0U &&
           configuration->jit_target.feature_mask == 0U;
}

runtimec_status_t binrwir_compiler_create(
    const runtimec_allocator_t *allocator,
    const binrwir_compiler_configuration_t *configuration,
    binrwir_compiler_t **output_compiler, runtimec_error_t *error) {
    binrwir_compiler_t *compiler;

    if (output_compiler != NULL) {
        *output_compiler = NULL;
    }
    if (!runtimec_allocator_is_valid(allocator) || configuration == NULL ||
        output_compiler == NULL ||
        configuration->structure_size != sizeof(*configuration) ||
        configuration->compiler_revision == 0U ||
        !binrwir_compiler_jit_configuration_is_valid(configuration)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (configuration->enable_jit) {
        
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_NOT_IMPLEMENTED, 1,
                                     "JIT compilation is not implemented yet");
    }
    compiler = (binrwir_compiler_t *)runtimec_allocator_allocate(
        allocator, sizeof(*compiler), _Alignof(binrwir_compiler_t));
    if (compiler == NULL) {
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 2,
                                     "compiler allocation failed");
    }
    compiler->allocator = *allocator;
    compiler->compiler_revision = configuration->compiler_revision;
    compiler->plan_generation = 0U;
    *output_compiler = compiler;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_compiler_destroy(binrwir_compiler_t *compiler,
                                           runtimec_error_t *error) {
    runtimec_allocator_t allocator;

    (void)error;
    if (compiler == NULL || !runtimec_allocator_is_valid(&compiler->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = compiler->allocator;
    return runtimec_allocator_deallocate(&allocator, compiler,
                                         sizeof(*compiler),
                                         _Alignof(binrwir_compiler_t));
}

runtimec_status_t binrwir_compiler_get_revision(
    const binrwir_compiler_t *compiler, uint32_t *output_compiler_revision) {
    if (output_compiler_revision != NULL) {
        *output_compiler_revision = UINT32_MAX;
    }
    if (compiler == NULL || output_compiler_revision == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_compiler_revision = compiler->compiler_revision;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_compiler_compile(
    binrwir_compiler_t *compiler, binrwir_binfunc_t *program,
    binrwir_compilation_t **output_compilation, runtimec_error_t *error) {
    binrwir_execution_plan_t *execution_plan = NULL;
    binrwir_compilation_t *compilation = NULL;
    uint64_t plan_generation;
    runtimec_error_t ignored;
    runtimec_status_t status;

    if (output_compilation != NULL) {
        *output_compilation = NULL;
    }
    
    if (program == NULL) {
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_INVALID, 1,
                                     "compiler received no program");
    }
    if (compiler == NULL || output_compilation == NULL) {
        runtimec_error_reset(&ignored);
        (void)binrwir_binfunc_destroy(program);
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_INVALID, 2,
                                     "compiler or output slot is missing");
    }
    if (binrwir_binfunc_validate(program) != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_reset(&ignored);
        (void)binrwir_binfunc_destroy(program);
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_INVALID, 3,
                                     "canonical program is not valid");
    }
    if (compiler->plan_generation == UINT64_MAX) {
        runtimec_error_reset(&ignored);
        (void)binrwir_binfunc_destroy(program);
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_INVALID, 4,
                                     "plan generation is exhausted");
    }
    plan_generation = compiler->plan_generation + 1U;
    status = binrwir_compiler_build_execution_plan(
        &compiler->allocator, program, plan_generation,
        compiler->compiler_revision, &execution_plan, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_reset(&ignored);
        (void)binrwir_binfunc_destroy(program);
        
        if (error != NULL && error->message.size == 0U) {
            return binrwir_compiler_fail(
                error, status, 7,
                "execution plan build failed without diagnosis");
        }
        return status;
    }
    
    compilation = (binrwir_compilation_t *)runtimec_allocator_allocate(
        &compiler->allocator, sizeof(*compilation),
        _Alignof(binrwir_compilation_t));
    if (compilation == NULL) {
        (void)binrwir_execution_plan_destroy(execution_plan);
        runtimec_error_reset(&ignored);
        (void)binrwir_binfunc_destroy(program);
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 5,
                                     "compilation allocation failed");
    }
    memset(compilation, 0, sizeof(*compilation));
    compilation->allocator = compiler->allocator;
    compilation->canonical_program = program;
    compilation->execution_plan = execution_plan;
    compilation->view.execution_target = BINRWIR_EXECUTION_TARGET_VMBIN;
    compilation->view.canonical_program = program;
    compilation->view.execution_plan = execution_plan;
    compilation->view.binrwir_jit_code = NULL;
    compiler->plan_generation = plan_generation;
    *output_compilation = compilation;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_compilation_get_view(
    const binrwir_compilation_t *compilation,
    binrwir_compilation_view_t *output_view, runtimec_error_t *error) {
    binrwir_compilation_reset_view(output_view);
    if (compilation == NULL || output_view == NULL ||
        compilation->canonical_program == NULL ||
        compilation->execution_plan == NULL) {
        return binrwir_compiler_fail(
            error, RUNTIMEC_STATUS_INVALID, 8,
            "compilation view is not readable (program or plan missing)");
    }
    *output_view = compilation->view;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_compilation_destroy(
    binrwir_compilation_t *compilation, runtimec_error_t *error) {
    runtimec_allocator_t allocator;
    runtimec_error_t ignored;
    runtimec_status_t status;

    (void)error;
    if (compilation == NULL ||
        !runtimec_allocator_is_valid(&compilation->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = compilation->allocator;
    runtimec_error_reset(&ignored);
    if (compilation->execution_plan != NULL) {
        status = binrwir_execution_plan_destroy(compilation->execution_plan);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    if (compilation->canonical_program != NULL) {
        status = binrwir_binfunc_destroy(compilation->canonical_program);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    return runtimec_allocator_deallocate(&allocator, compilation,
                                         sizeof(*compilation),
                                         _Alignof(binrwir_compilation_t));
}
