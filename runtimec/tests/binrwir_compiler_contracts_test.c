/* Verify BinRWIR Compiler and its JIT compile-versus-execute boundaries. */

#include <stdint.h>
#include <stdio.h>

#include "binrwir_compiler/compiler.h"
#include "binrwir_compiler/jit/compiler.h"
#include "binrwir_compiler/jit/execution_interface.h"
#include "binrwir_compiler/jit/executor.h"
#include "binrwir_compiler/jit/register_allocation.h"
#include "binrwir_compiler/jit/safepoint.h"
#include "binrwir_compiler/optimization.h"
#include "vmkv/encoder.h"

#define RUNTIMEC_ASSERT_FUNCTION_TYPE(function_name, function_pointer_type)       \
    _Static_assert(_Generic(&(function_name), function_pointer_type: 1,           \
                            default: 0),                                          \
                   "unexpected declaration for " #function_name)

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                         \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

typedef runtimec_status_t (*binrwir_compile_function_t)(
    binrwir_compiler_t *, const binrwir_program_t *,
    binrwir_compilation_t **, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_compiler_revision_function_t)(
    const binrwir_compiler_t *, uint32_t *);
typedef runtimec_status_t (*binrwir_build_execution_plan_function_t)(
    const runtimec_allocator_t *, const binrwir_program_t *, uint64_t,
    uint32_t, binrwir_execution_plan_t **, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_compile_function_t)(
    binrwir_jit_compiler_t *, const binrwir_program_t *,
    const binrwir_execution_plan_t *,
    binrwir_jit_compilation_result_t *, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_execute_function_t)(
    const binrwir_jit_code_t *, binrwir_execution_frame_t *,
    binrwir_execution_exit_t *, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_register_allocation_create_function_t)(
    const binrwir_jit_verified_program_t *, const binrwir_execution_plan_t *,
    const binrwir_jit_target_t *, const runtimec_allocator_t *,
    binrwir_jit_register_allocation_t **, binrwir_jit_refusal_t *,
    runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_safepoint_plan_create_function_t)(
    const binrwir_jit_verified_program_t *,
    const binrwir_jit_register_allocation_t *, const runtimec_allocator_t *,
    binrwir_jit_safepoint_plan_t **, binrwir_jit_refusal_t *,
    runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_safepoint_commit_function_t)(
    const binrwir_jit_safepoint_plan_t *,
    binrwir_jit_safepoint_boundary_t, uint32_t, uint32_t, void *,
    binrwir_execution_frame_t *, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_safepoint_discard_function_t)(
    const binrwir_jit_safepoint_plan_t *,
    binrwir_jit_safepoint_boundary_t, uint32_t, void *,
    binrwir_execution_frame_t *, runtimec_error_t *);
typedef runtimec_status_t (*vmkv_encoder_revision_function_t)(
    const vmkv_encoder_t *, uint32_t *);

RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_compile, binrwir_compile_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_get_revision, binrwir_compiler_revision_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_build_execution_plan,
    binrwir_build_execution_plan_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(binrwir_jit_compile, binrwir_jit_compile_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(binrwir_jit_execute, binrwir_jit_execute_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_jit_register_allocation_create,
    binrwir_jit_register_allocation_create_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_jit_safepoint_plan_create,
    binrwir_jit_safepoint_plan_create_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_jit_safepoint_commit_completed_state,
    binrwir_jit_safepoint_commit_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_jit_safepoint_discard_incomplete_state,
    binrwir_jit_safepoint_discard_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    vmkv_encoder_get_canonical_encoding_revision,
    vmkv_encoder_revision_function_t);

_Static_assert(BINRWIR_EXECUTION_TARGET_INVALID == 0,
               "invalid execution target changed");
_Static_assert(BINRWIR_EXECUTION_TARGET_VMBIN == 1,
               "vmbin execution target changed");
_Static_assert(BINRWIR_EXECUTION_TARGET_JIT == 2,
               "JIT execution target changed");

_Static_assert(BINRWIR_JIT_COMPILATION_OUTCOME_INVALID == 0,
               "invalid JIT compilation outcome changed");
_Static_assert(BINRWIR_JIT_COMPILATION_OUTCOME_COMPILED == 1,
               "compiled JIT outcome changed");
_Static_assert(BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE == 2,
               "not-compilable JIT outcome changed");
_Static_assert(BINRWIR_JIT_COMPILATION_OUTCOME_ERROR == 3,
               "error JIT outcome changed");

_Static_assert(sizeof(binrwir_jit_body_origin_t) == 16U,
               "JIT body origins must remain sixteen bytes");
_Static_assert(sizeof(binrwir_jit_location_mapping_t) == 24U,
               "JIT location mappings must remain twenty-four bytes");
_Static_assert(sizeof(binrwir_jit_materialization_entry_t) == 24U,
               "JIT materialization entries must remain twenty-four bytes");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION == 1,
               "instruction-completion boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_PAUSE == 2,
               "pause boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_HANDOFF == 3,
               "handoff boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_CALL == 4,
               "call boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_EXTERNAL_OBSERVATION == 5,
               "external-observation boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_RECOVERY == 6,
               "recovery boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_RESUME == 7,
               "resume boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_GUARD_FAILURE == 8,
               "guard-failure boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_ERROR == 9,
               "error boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_ACTION_COMMIT_COMPLETED_STATE == 1,
               "completed-state commit action changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_ACTION_DISCARD_INCOMPLETE_STATE == 2,
               "incomplete-state discard action changed");
_Static_assert(BINRWIR_JIT_MATERIALIZATION_FLAG_COMPLETED_DIRTY == 1,
               "completed dirty materialization flag changed");
_Static_assert(BINRWIR_JIT_WRITE_ALIAS_ACTION_UPDATE_READ_LOCATION == 1,
               "write alias update action changed");
_Static_assert(BINRWIR_JIT_WRITE_ALIAS_ACTION_INVALIDATE_READ_LOCATION == 2,
               "write alias invalidation action changed");

static int failures;

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    return NULL;
}

static void test_deallocate(
    void *context, void *allocation, size_t size, size_t alignment) {
    (void)context;
    (void)allocation;
    (void)size;
    (void)alignment;
}

static void test_compiler_configuration(void) {
    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    binrwir_compiler_t *compiler =
        (binrwir_compiler_t *)(uintptr_t)1U;
    binrwir_compiler_configuration_t configuration = {
        .structure_size = sizeof(configuration),
        .compiler_revision = 1U,
        .enable_jit = false,
        .jit_machine_code_capacity = 0U,
        .jit_target = {0},
    };

    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(compiler == NULL);

    configuration.compiler_revision = 0U;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler == NULL);
    configuration.compiler_revision = 1U;

    configuration.jit_machine_code_capacity = 1U;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler == NULL);
    configuration.jit_machine_code_capacity = 0U;

    configuration.enable_jit = true;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler == NULL);

    configuration.jit_machine_code_capacity = 1U;
    configuration.jit_target.architecture = BINRWIR_JIT_ARCHITECTURE_X86_64;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(compiler == NULL);
}

static void test_compiler_revision_failure_shape(void) {
    uint32_t compiler_revision = 0U;

    RUNTIMEC_EXPECT(binrwir_compiler_get_revision(
        NULL, &compiler_revision) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler_revision == UINT32_MAX);
}

static void test_encoder_revision_failure_shape(void) {
    uint32_t canonical_encoding_revision = 0U;

    RUNTIMEC_EXPECT(vmkv_encoder_get_canonical_encoding_revision(
        NULL, &canonical_encoding_revision) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(canonical_encoding_revision == UINT32_MAX);
}

static void test_compilation_shapes(void) {
    const binrwir_jit_compilation_result_t not_compilable = {
        .outcome = BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE,
        .code = NULL,
        .refusal = {.reason = BINRWIR_JIT_REFUSAL_UNSUPPORTED_PROGRAM},
    };
    binrwir_compilation_view_t view = {
        .execution_target = BINRWIR_EXECUTION_TARGET_JIT,
        .canonical_program = (const binrwir_program_t *)(uintptr_t)1U,
        .execution_plan = (const binrwir_execution_plan_t *)(uintptr_t)1U,
        .binrwir_jit_code = (const binrwir_jit_code_t *)(uintptr_t)1U,
    };

    RUNTIMEC_EXPECT(not_compilable.outcome ==
                    BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE);
    RUNTIMEC_EXPECT(not_compilable.code == NULL);
    RUNTIMEC_EXPECT(binrwir_compilation_get_view(NULL, &view, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(view.execution_target == BINRWIR_EXECUTION_TARGET_INVALID);
    RUNTIMEC_EXPECT(view.canonical_program == NULL);
    RUNTIMEC_EXPECT(view.execution_plan == NULL);
    RUNTIMEC_EXPECT(view.binrwir_jit_code == NULL);
}

static void test_optimization_failure_shape(void) {
    binrwir_execution_plan_t *execution_plan =
        (binrwir_execution_plan_t *)(uintptr_t)1U;

    RUNTIMEC_EXPECT(binrwir_compiler_build_execution_plan(
        NULL, NULL, 0U, 0U, &execution_plan, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(execution_plan == NULL);
}

static void test_jit_failure_shapes(void) {
    binrwir_jit_compilation_result_t compilation_result = {
        .outcome = BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE,
        .code = (binrwir_jit_code_t *)(uintptr_t)1U,
    };
    binrwir_execution_exit_t execution_exit = {
        .kind = BINRWIR_EXECUTION_EXIT_DONE,
        .status = RUNTIMEC_STATUS_SUCCESS,
    };

    RUNTIMEC_EXPECT(binrwir_jit_compile(NULL, NULL, NULL, &compilation_result, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compilation_result.outcome ==
                    BINRWIR_JIT_COMPILATION_OUTCOME_INVALID);
    RUNTIMEC_EXPECT(compilation_result.code == NULL);

    RUNTIMEC_EXPECT(binrwir_jit_execute(NULL, NULL, &execution_exit, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(execution_exit.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    RUNTIMEC_EXPECT(execution_exit.status == RUNTIMEC_STATUS_INVALID);
}

static void test_jit_register_and_safepoint_failure_shapes(void) {
    binrwir_jit_register_allocation_t *register_allocation =
        (binrwir_jit_register_allocation_t *)(uintptr_t)1U;
    binrwir_jit_safepoint_plan_t *safepoint_plan =
        (binrwir_jit_safepoint_plan_t *)(uintptr_t)1U;
    binrwir_jit_refusal_t refusal = {
        .reason = BINRWIR_JIT_REFUSAL_NONE,
    };
    binrwir_jit_execution_interface_t execution_interface = {
        .binary_interface_major_version = UINT16_MAX,
    };
    binrwir_execution_frame_t frame = {
        .logical_program_counter = 11U,
    };

    RUNTIMEC_EXPECT(binrwir_jit_register_allocation_create(
        NULL, NULL, NULL, NULL, &register_allocation, &refusal, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(register_allocation == NULL);
    RUNTIMEC_EXPECT(refusal.reason == BINRWIR_JIT_REFUSAL_INVALID_PROGRAM);

    refusal.reason = BINRWIR_JIT_REFUSAL_NONE;
    RUNTIMEC_EXPECT(binrwir_jit_safepoint_plan_create(
        NULL, NULL, NULL, &safepoint_plan, &refusal, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(safepoint_plan == NULL);
    RUNTIMEC_EXPECT(refusal.reason == BINRWIR_JIT_REFUSAL_INVALID_PROGRAM);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_commit_completed_state(
        NULL, BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION, 11U, 12U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_commit_completed_state(
        (const binrwir_jit_safepoint_plan_t *)(uintptr_t)1U,
        BINRWIR_JIT_SAFEPOINT_BOUNDARY_ERROR, 11U, 12U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_commit_completed_state(
        (const binrwir_jit_safepoint_plan_t *)(uintptr_t)1U,
        BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION, 11U, 12U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_discard_incomplete_state(
        (const binrwir_jit_safepoint_plan_t *)(uintptr_t)1U,
        BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION, 11U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_discard_incomplete_state(
        (const binrwir_jit_safepoint_plan_t *)(uintptr_t)1U,
        BINRWIR_JIT_SAFEPOINT_BOUNDARY_ERROR, 11U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_execution_interface(&execution_interface) ==
                    RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(execution_interface.binary_interface_major_version == 0U);
    RUNTIMEC_EXPECT(execution_interface.value_body_address_size_bytes == 0U);
    RUNTIMEC_EXPECT(execution_interface.argument_index_size_bytes == 0U);
    RUNTIMEC_EXPECT(execution_interface.safepoint_contract_revision == 0U);
}

int main(void) {
    test_compiler_configuration();
    test_compiler_revision_failure_shape();
    test_encoder_revision_failure_shape();
    test_compilation_shapes();
    test_optimization_failure_shape();
    test_jit_failure_shapes();
    test_jit_register_and_safepoint_failure_shapes();
    if (failures != 0) {
        fprintf(stderr, "binrwir_compiler_contracts_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
