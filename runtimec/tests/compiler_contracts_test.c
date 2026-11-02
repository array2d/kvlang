/* Verify BinRWIR Compiler and vmjit compile-versus-execute boundaries. */

#include <stdint.h>
#include <stdio.h>

#include "compiler/compiler.h"
#include "compiler/optimization.h"
#include "vmjit/compiler.h"
#include "vmjit/vmjit.h"
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
typedef runtimec_status_t (*vmjit_compile_function_t)(
    vmjit_compiler_t *, const binrwir_program_t *,
    const binrwir_execution_plan_t *,
    vmjit_compilation_result_t *, runtimec_error_t *);
typedef runtimec_status_t (*vmjit_execute_function_t)(
    const vmjit_code_t *, binrwir_execution_frame_t *,
    binrwir_execution_exit_t *, runtimec_error_t *);
typedef runtimec_status_t (*vmkv_encoder_revision_function_t)(
    const vmkv_encoder_t *, uint32_t *);

RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_compile, binrwir_compile_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_get_revision, binrwir_compiler_revision_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_build_execution_plan,
    binrwir_build_execution_plan_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(vmjit_compile, vmjit_compile_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(vmjit_execute, vmjit_execute_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    vmkv_encoder_get_canonical_encoding_revision,
    vmkv_encoder_revision_function_t);

_Static_assert(BINRWIR_EXECUTION_TARGET_INVALID == 0,
               "invalid execution target changed");
_Static_assert(BINRWIR_EXECUTION_TARGET_VMBIN == 1,
               "vmbin execution target changed");
_Static_assert(BINRWIR_EXECUTION_TARGET_VMJIT == 2,
               "vmjit execution target changed");

_Static_assert(VMJIT_COMPILATION_OUTCOME_INVALID == 0,
               "invalid vmjit compilation outcome changed");
_Static_assert(VMJIT_COMPILATION_OUTCOME_COMPILED == 1,
               "compiled vmjit outcome changed");
_Static_assert(VMJIT_COMPILATION_OUTCOME_NOT_COMPILABLE == 2,
               "not-compilable vmjit outcome changed");
_Static_assert(VMJIT_COMPILATION_OUTCOME_ERROR == 3,
               "error vmjit outcome changed");

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
        .enable_vmjit = false,
        .vmjit_compiler = NULL,
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

    configuration.vmjit_compiler = (vmjit_compiler_t *)(uintptr_t)1U;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler == NULL);

    configuration.enable_vmjit = true;
    configuration.vmjit_compiler = NULL;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_INVALID);
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
    const vmjit_compilation_result_t not_compilable = {
        .outcome = VMJIT_COMPILATION_OUTCOME_NOT_COMPILABLE,
        .code = NULL,
        .refusal = {.reason = VMJIT_REFUSAL_UNSUPPORTED_PROGRAM},
    };
    binrwir_compilation_view_t view = {
        .execution_target = BINRWIR_EXECUTION_TARGET_VMJIT,
        .canonical_program = (const binrwir_program_t *)(uintptr_t)1U,
        .execution_plan = (const binrwir_execution_plan_t *)(uintptr_t)1U,
        .vmjit_code = (const vmjit_code_t *)(uintptr_t)1U,
    };

    RUNTIMEC_EXPECT(not_compilable.outcome ==
                    VMJIT_COMPILATION_OUTCOME_NOT_COMPILABLE);
    RUNTIMEC_EXPECT(not_compilable.code == NULL);
    RUNTIMEC_EXPECT(binrwir_compilation_get_view(NULL, &view, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(view.execution_target == BINRWIR_EXECUTION_TARGET_INVALID);
    RUNTIMEC_EXPECT(view.canonical_program == NULL);
    RUNTIMEC_EXPECT(view.execution_plan == NULL);
    RUNTIMEC_EXPECT(view.vmjit_code == NULL);
}

static void test_optimization_failure_shape(void) {
    binrwir_execution_plan_t *execution_plan =
        (binrwir_execution_plan_t *)(uintptr_t)1U;

    RUNTIMEC_EXPECT(binrwir_compiler_build_execution_plan(
        NULL, NULL, 0U, 0U, &execution_plan, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(execution_plan == NULL);
}

static void test_vmjit_failure_shapes(void) {
    vmjit_compilation_result_t compilation_result = {
        .outcome = VMJIT_COMPILATION_OUTCOME_NOT_COMPILABLE,
        .code = (vmjit_code_t *)(uintptr_t)1U,
    };
    binrwir_execution_exit_t execution_exit = {
        .kind = BINRWIR_EXECUTION_EXIT_DONE,
        .status = RUNTIMEC_STATUS_SUCCESS,
    };

    RUNTIMEC_EXPECT(vmjit_compile(NULL, NULL, NULL, &compilation_result, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compilation_result.outcome ==
                    VMJIT_COMPILATION_OUTCOME_INVALID);
    RUNTIMEC_EXPECT(compilation_result.code == NULL);

    RUNTIMEC_EXPECT(vmjit_execute(NULL, NULL, &execution_exit, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(execution_exit.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    RUNTIMEC_EXPECT(execution_exit.status == RUNTIMEC_STATUS_INVALID);
}

int main(void) {
    test_compiler_configuration();
    test_compiler_revision_failure_shape();
    test_encoder_revision_failure_shape();
    test_compilation_shapes();
    test_optimization_failure_shape();
    test_vmjit_failure_shapes();
    if (failures != 0) {
        fprintf(stderr, "compiler_contracts_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
